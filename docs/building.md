# Building Pane

Pane is a Swift app (AppKit + SwiftUI) on Pane's own Chromium build, `PaneCore.framework`, embedded in-process the way Arc and Dia embed theirs. Why: `docs/adr/0004-native-shell-on-own-chromium-framework.md`. What was decided, and why: `docs/decisions.md`.

## Layout

- `engine/` — everything that becomes `PaneCore.framework`
  - `CHROMIUM_VERSION` — the pinned Chromium stable tag
  - `args/` — gn args per build config
  - `patches/` — `series` (apply order), `MANIFEST` (each touched Chromium file → its owning patch), the patches
  - `pane/` — the `//pane` tree, symlinked into the checkout as `src/pane`
  - `scripts/` — `sync.sh`, `apply-patches.sh`, `build.sh`, `make-patch.sh`, `package.sh`, `dist.sh`, and the prebuilt engine's `engine-key.sh`, `fetch-engine.sh`, `publish-engine.sh`
- `app/` — the Swift app, the `PaneKit` package, `VERSION`, and the dmg's resources (`dmg/`)
- `tests/` — the scenario runner (`run.ts`), fixtures (`pages/`), scenarios
- `third_party/` — code Pane adapts; `NOTICE` gives origins and licenses
- `docs/` — this guide, `decisions.md` (the dated engineering log), `human-checklist.md` (what only a person can do or verify), and the ADRs
- `evidence/` — gitignored output of acceptance checks, per phase

## Chromium checkout

The checkout lives outside the repo, at `~/chromium` (override with `CHROMIUM_ROOT`): `depot_tools/` and `src/`. Keep it out of Spotlight — here `~/chromium` is a symlink to `~/chromium.noindex`, which Spotlight skips; indexing a fresh checkout during a build starved the system into a kernel panic once. It needs full Xcode plus its Metal toolchain (`xcodebuild -downloadComponent MetalToolchain`), an APFS volume, and ~100 GB free.

```bash
engine/scripts/sync.sh            # depot_tools, src at CHROMIUM_VERSION, deps, hooks, //pane link
engine/scripts/apply-patches.sh   # apply patches/series (no-op when unchanged)
engine/scripts/build.sh           # out/Release, target `chrome` (args/release.gn)
```

Every script is safe to re-run. The first sync and the first build take hours; later builds are incremental. Moving to a newer Chromium is a routine: `engine/UPGRADING.md`.

## Patches

Edit files in `~/chromium/src`, list each touched file in `patches/MANIFEST` with its owning patch, then regenerate the patch with `engine/scripts/make-patch.sh <patch-name>`. A file belongs to exactly one patch, so a patch is the diff of its own files against the pinned tag.

## Scenarios

```bash
bun tests/run.ts --binary <path to browser executable> --out <dir> <scenario>...
```

The runner serves `tests/pages` on 127.0.0.1, launches the binary with remote debugging, runs each scenario, and writes `<scenario>.json` plus `report.json` into `--out`. `PANE_SCENARIO_STDERR=1` shows the binary's log. A scenario fails after `--timeout` seconds (600). Whatever it launched is quit, and the directories it took (`context.tempDir`) are removed, with any process still using them. Each entry records whether the screen was locked and the checks it deferred; `report.json`'s `complete` is true only when every check ran.

Scenarios for the Pane app (`lib/pane.ts`) give it throwaway data (`PANE_DATA_DIR`) and drive it through its automation channel (`PANE_AUTOMATION_DIR`, `app/Pane/Automation/`): JSON commands that call the same controller methods the UI does, key presses posted as real `NSEvent`s, and a state snapshot with every reply. CDP reads pages; `lib/cua.ts` covers what only the screen shows, and needs an unlocked session.

## App

```bash
engine/scripts/package.sh   # app/build/Pane.app: the engine bundle with the Swift app inside, signed
engine/scripts/dist.sh      # app/release/Pane-<version>-arm64.dmg: package.sh (stripped), then the dmg
(cd app/PaneKit && swift test)
```

The app's version lives in `app/VERSION`, its only source: the bundle's version keys, Settings, the update check, and the dmg's name all come from it.

## Prebuilt engine

The app builds without a Chromium checkout on a prebuilt engine, as Ardia's does:

```bash
PANE_ENGINE_OUT="$(engine/scripts/fetch-engine.sh)" engine/scripts/package.sh
```

`engine/prebuilt-engines.tsv` names, per engine key (`engine-key.sh`: the git trees of `CHROMIUM_VERSION`, `args`, `patches`, and `pane`), the GitHub release that carries `PaneCore-<chromium>-<key>.zip`, and its SHA-256. `fetch-engine.sh` downloads and verifies it into `app/build/engines/`. A checkout whose engine sources changed has no prebuilt: build the engine. `PANE_ENGINE_ARCHIVE=1 dist.sh` (or `package.sh`) archives the engine it was built from into `app/release/` and prints its line for the TSV; `publish-engine.sh` archives it, publishes it as the `engine-<key:12>` prerelease, and adds that line. Archiving refuses an engine that `build.sh` did not record the current key for (`out/Release/pane-engine.key`): one built before a change to the engine's sources, or from a checkout whose tag, patches, or `//pane` were not this repo's.
