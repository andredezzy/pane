# Pane native

The native macOS Pane: a Swift app (AppKit + SwiftUI) on Pane's own Chromium build, `PaneCore.framework`, embedded in-process the way Arc and Dia embed theirs. Why: `docs/adr/0004-native-shell-on-own-chromium-framework.md`. Plan and progress: `docs/superpowers/plans/2026-10-26-native-shell-arc-architecture.md`.

## Layout

- `engine/` — everything that becomes `PaneCore.framework`
  - `CHROMIUM_VERSION` — the pinned Chromium stable tag
  - `args/` — gn args per build config
  - `patches/` — `series` (apply order), `MANIFEST` (each touched Chromium file → its owning patch), the patches
  - `pane/` — the `//pane` tree, symlinked into the checkout as `src/pane`
  - `scripts/` — `sync.sh`, `apply-patches.sh`, `build.sh`, `make-patch.sh`
  - `tests/` — the scenario runner (`run.ts`), fixtures (`pages/`), scenarios
- `app/` — the Swift app and the `PaneKit` package
- `evidence/` — gitignored output of acceptance checks, per phase
- `DECISIONS.md` — dated engineering log of choices made during the build
- `HUMAN-CHECKLIST.md` — what only a person can do or verify
- `NOTICE` — origins and licenses of third-party patches

## Chromium checkout

The checkout lives outside the repo, at `~/chromium` (override with `CHROMIUM_ROOT`): `depot_tools/` and `src/`. Keep it out of Spotlight — here `~/chromium` is a symlink to `~/chromium.noindex`, which Spotlight skips; indexing a fresh checkout during a build starved the system into a kernel panic once. It needs full Xcode plus its Metal toolchain (`xcodebuild -downloadComponent MetalToolchain`), an APFS volume, and ~100 GB free.

```bash
native/engine/scripts/sync.sh            # depot_tools, src at CHROMIUM_VERSION, deps, hooks, //pane link
native/engine/scripts/apply-patches.sh   # apply patches/series (no-op when unchanged)
native/engine/scripts/build.sh           # out/Release, target `chrome` (args/release.gn)
```

Every script is safe to re-run. The first sync and the first build take hours; later builds are incremental.

## Patches

Edit files in `~/chromium/src`, list each touched file in `patches/MANIFEST` with its owning patch, then regenerate the patch with `native/engine/scripts/make-patch.sh <patch-name>`. A file belongs to exactly one patch, so a patch is the diff of its own files against the pinned tag.

## Scenarios

```bash
bun native/engine/tests/run.ts --binary <path to browser executable> --out <dir> <scenario>...
```

The runner serves `tests/pages` on 127.0.0.1, launches the binary with remote debugging, runs each scenario, and writes `<scenario>.json` plus `report.json` into `--out`. `PANE_SCENARIO_STDERR=1` shows the binary's log.

Scenarios for the Pane app (`lib/pane.ts`) give it throwaway data (`PANE_DATA_DIR`) and drive it through its automation channel (`PANE_AUTOMATION_DIR`, `app/Pane/Automation/`): JSON commands that call the same controller methods the UI does, key presses posted as real `NSEvent`s, and a state snapshot with every reply. CDP reads pages; `lib/cua.ts` covers what only the screen shows, and needs an unlocked session.

## App

```bash
native/engine/scripts/package.sh   # app/build/Pane.app: the engine bundle with the Swift app inside, signed
(cd native/app/PaneKit && swift test)
```
