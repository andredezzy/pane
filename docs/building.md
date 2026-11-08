# Building Pane

Pane is a Swift app (AppKit + SwiftUI) on Pane's own Chromium build, `Pane Framework.framework`, embedded in-process the way Arc and Dia embed theirs. Why: `docs/adr/0004-native-shell-on-own-chromium-framework.md`. What was decided, and why: the ADRs in `docs/adr/`, one per subject.

## Layout

- `engine/` — everything that becomes `Pane Framework.framework`
  - `CHROMIUM_VERSION` — the pinned Chromium stable tag
  - `args/` — gn args per build config
  - `patches/` — `series` (apply order), `MANIFEST` (each touched Chromium file → its owning patch), the patches
  - `pane/` — the `//pane` tree, symlinked into the checkout as `src/pane`
  - `scripts/` — `sync.sh`, `apply-patches.sh`, `build.sh`, `make-patch.sh`, `preflight.sh`, `fetch-crlset.sh`, `package.sh`, `dist.sh`, and the prebuilt engine's `engine-key.sh`, `fetch-engine.sh`, `publish-engine.sh`
- `app/` — the Swift app, the `PaneKit` package, the volume's watchdog (`VolumeWatch/`), `VERSION`, the entitlements (`signing/`), the dmg's resources (`dmg/`, with `make-background.swift` and `make-volume-icon.swift`, which make its background and volume icon), and the icon's sources (`icon/`: the window's renders, and `make-icons.swift`, which makes the app's icons from them)
- `tests/` — the scenario runner (`run.ts`), fixtures (`pages/`), scenarios that check (`scenarios/`), what measures and never judges (`benchmarks/`), and a held test copy for a pass by hand (`rigs/`)
- `third_party/` — code Pane adapts; `NOTICE` gives origins and licenses
- `docs/` — this guide, `human-checklist.md` (what only a person can do or verify), `qa/`, reports of passes by computer use, `adr/`, one record per subject, `benchmarks/`, measured comparisons of Pane builds with their raw data and how to rerun them, and `growth/`, the media Pane is shown with outside the app
- `evidence/` — gitignored output of acceptance checks

## Chromium checkout

The checkout lives outside the repo, at `~/chromium` (override with `CHROMIUM_ROOT`): `depot_tools/` and `src/`. Keep it out of Spotlight — here `~/chromium` is a symlink to `~/chromium.noindex`, which Spotlight skips; indexing a fresh checkout during a build starved the system into a kernel panic once. It needs full Xcode plus its Metal toolchain (`xcodebuild -downloadComponent MetalToolchain`), an APFS volume, and ~100 GB free.

```bash
engine/scripts/sync.sh            # depot_tools, src at CHROMIUM_VERSION, deps, hooks, //pane link
engine/scripts/apply-patches.sh   # apply patches/series (no-op when unchanged)
engine/scripts/build.sh           # out/Release, target `chrome` (args/release.gn)
```

Every script is safe to re-run. The first sync and the first build take hours; later builds are incremental. The checkout syncs Windows' dependencies and Chrome's PGO profiles too (ADR 0007); to compile for Windows, sync once with `PANE_WIN_TOOLCHAIN_DIR` naming the folder that holds the toolchain's zip, packaged from a Visual Studio install (Chromium's `docs/windows_build_instructions.md`, Packaging), which stays outside the repo. Moving to a newer Chromium is a routine: `engine/UPGRADING.md`.

## Patches

Edit files in `~/chromium/src`, list each touched file in `patches/MANIFEST` with its owning patch, then regenerate the patch with `engine/scripts/make-patch.sh <patch-name>`. A file belongs to exactly one patch, so a patch is the diff of its own files against the pinned tag.

## Engine unit tests

```bash
engine/scripts/build.sh pane_unittests
~/chromium/src/out/Release/pane_unittests
```

`pane_unittests` checks `//pane/encryption`, the formats every profile file is written in (ADR 0121): the key registry, the SQLite VFS through journals, WAL, kills, Raze and Recovery, and the stream and whole-file codecs. CI never builds Chromium, so these run locally, before committing an engine change.

## Scenarios

```bash
bun tests/run.ts --binary <path to browser executable> --out <dir> [--foreground] <scenario or path>...
```

The runner serves `tests/pages` on 127.0.0.1, launches the binary with remote debugging, runs each scenario, and writes `<scenario>.json` plus `report.json` into `--out`. `PANE_SCENARIO_STDERR=1` shows the binary's log. A scenario fails after `--timeout` seconds (600). Whatever it launched is quit, and the directories it took (`context.tempDir`) are removed, with any process still using them. Each entry records whether the screen was locked and the checks it deferred; `report.json`'s `complete` is true only when every check ran.

Scenarios for the Pane app (`lib/pane.ts`) give it throwaway data (`PANE_DATA_DIR`) and a throwaway home (`HOME` and `CFFIXED_USER_HOME`, so its caches, HTTP storage, Downloads, and Crashpad's database are not the user's Pane's), and drive it through its automation channel (`PANE_AUTOMATION_DIR`, `app/Pane/Automation/`), which, with every `PANE_` hook, works only under `--use-mock-keychain` (`TestHooks`), where none of the user's data opens; the engine passes Chromium no switch of a launch without it but Pane's own: JSON commands that call the same controller methods the UI does, key presses posted as real `NSEvent`s, and a state snapshot with every reply. CDP reads pages; `lib/cua.ts` covers what only the screen shows, and needs an unlocked session and cua-driver's daemon (`canDriveUI()`), or the checks defer. Scenarios run on a Mac in use, so the app never takes the front from the app its user is in: what needs Pane in front (keys a panel takes, a page's fullscreen, Quit Pane?) defers unless `--foreground` allows it, which CI does and an idle Mac can. Preferences are the one thing they still share with the user's Pane: cfprefsd keeps them by bundle ID and ignores a fixed home, and a copy under another bundle ID would ask again for what macOS granted Pane (pasting from other apps), which closes its menus. So a run sets Chromium's `LastRunAppBundlePath` to the build, which the user's Pane resets when it next starts.

## Rigs

A person's pass, by computer use, runs on a test copy `tests/rigs/` holds open, on throwaway data, and never on the user's Pane:

```bash
bun tests/run.ts --binary <copy of Pane>/Contents/MacOS/Pane --out <dir> --timeout 7300 tests/rigs/qa-hold.ts  # PIN 1234; ends at <dir>/qa-stop
bun tests/rigs/qa-send.ts <dir>/qa-session.json <command> [json]                                               # setup, through the automation channel
bun tests/rigs/front.ts guard [idle seconds]                                                                   # exit 3 unless the Mac is that idle and unlocked
bun tests/rigs/front.ts <dir>/qa-session.json <step>...                                                        # keys and captures
bun tests/rigs/frame-timing.ts <dir>/qa-session.json <out.mov> <seconds> <step>...                             # when the picture changed, from a recording
```

Taking the front needs the user's word in the session, or the Mac idle 10 minutes with its screen unlocked (`front.ts guard`). `front.ts` checks before each key that the test copy's pid is the frontmost app's and that no input but its own has come, and stops with exit 3 at the first sign of the user: nothing the user's gets keys. It captures the test copy's window, never the screen.

## Testing as a person would

Scenarios prove the mechanics; a person checks what they see and do. A release candidate gets this pass too, driven by computer use, in light and dark, with every finding fixed and checked again the same way. Reports go in `docs/qa/<subject>/`, naming their screenshots, which stay in `evidence/`.

The rig: cua-driver (`tests/lib/cua.ts`), `screencapture -l <window id>` for stills, `screencapture -v` for motion, `tests/rigs/qa-hold.ts` to hold a test copy open, and `tests/rigs/qa-send.ts` for setup only, never in place of the input under test.

**Taking the screen.** The Mac is in use, so:

- **Always a test copy,** through the scenario harness, on throwaway data. Never the user's Pane, `/Applications`, the user's data, or the user's accounts.
- **Background first.** Whatever accessibility does on a window not in front runs at any time.
- **The front, only when allowed:** the user has said so in the session, or the Mac has been idle at least 10 minutes with its screen unlocked (`HIDIdleTime` from `ioreg -c IOHIDSystem`, `CGSSessionScreenIsLocked`). Say first what will run, for how long, and that any key or click stops it. Check again before each step, stop at the first sign of the user, and put back the app that was in front.
- **Before every key, prove the target:** the frontmost app's pid is the test copy's, immediately before each key. Nothing of the user's gets keys, and only windows proven to be the test's get captured. `tests/rigs/` has the guard.
- **If neither comes within a working day,** ask once.

**The checklist:**

1. **Lock and unlock:** the PIN typed with real keys, a wrong one, wrong ones in Settings, ⌃⌘L, the menu, the idle lock with time scaled, the PIN screen against Paper 07.1 to 07.9, the tab that showed comes back, keypress to first frame from a recording.
2. **Real browsing,** on throwaway profiles: public sites that need no account, the sign-in fixture surviving a relaunch and a lock, address suggestions after a relaunch, a download through the UI, an extension from the Web Store.
3. **What changed:** the screens a release touches against their boards, each compared by pixel with the last release.
4. **An older Pane's data:** 07.8 for 0.15.2's data, and nothing changed.
5. **The files from another app,** in Terminal, on test data only: `strings` and `sqlite3` on History, Cookies, Local Storage, an IndexedDB file, and a cache file.
6. **Speed as felt:** recordings of the same page loads on the last release and the candidate, timed frame by frame.
7. **Open items in `docs/human-checklist.md`** an agent may do.

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

`engine/prebuilt-engines.tsv` names the engine key (`engine-key.sh`: the git trees of `CHROMIUM_VERSION`, `args`, `patches`, and `pane`) of the `engine-<key:12>` release that carries `PaneCore-<chromium>-<key:12>.zip`, and its SHA-256. `fetch-engine.sh` downloads and verifies it into `app/build/engines/`. A checkout whose engine sources changed has no prebuilt: build the engine. `publish-engine.sh` archives the engine into `app/release/`, publishes it as the prerelease, and overwrites that line. Archiving refuses an engine that `build.sh` did not record the current key for (`out/Release/pane-engine.key`): one built before a change to the engine's sources, or from a checkout whose tag, patches, or `//pane` were not this repo's.

## Reading a crash report

A release strips its executables, so a crash report (`~/Library/Logs/DiagnosticReports/Pane-*.ips`) names addresses, not functions. Its symbols are published: the app's beside the dmg in the release (`Pane-<version>-arm64.dSYM.zip`), and PaneCore's beside the engine archive in the `engine-<key:12>` prerelease (`PaneCore-<chromium>-<key:12>.dSYM.zip`), where `<key>` is the copy's `PaneEngineKey` (`plutil -extract PaneEngineKey raw Pane.app/Contents/Info.plist`). Unzip both, check that their UUIDs (`dwarfdump --uuid`) are the report's images', and resolve each frame against its image's load address:

```bash
atos -arch arm64 -o "Pane Framework.dSYM/Contents/Resources/DWARF/Pane Framework" -l <load address> <address>...
```
