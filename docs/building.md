# Building Pane

Pane is its own build of Chromium: Chromium's loader and helpers, and `Pane Framework.framework`, which holds Chrome's `chrome/` layer and the `//pane` tree, Pane's code, its UI drawn in C++ with Views, as Chrome's own is. gn builds all of it. Why: `docs/adr/0004-native-shell-on-own-chromium-framework.md` and `docs/adr/0124-one-ui-in-views-on-every-os.md`. What was decided, and why: the ADRs in `docs/adr/`, one per subject.

## Layout

- `pane/` — the `//pane` tree, symlinked into the checkout as `src/pane`, linked into `Pane Framework.framework`: the launch (`app/`), the model and the data's formats and rules (`model/`: the lock, the vault's sequences, the data's lock, and the panic wipe's order, each system's own steps in `_mac`/`_win` halves, Windows' engine folder as the vault's place and its secret under DPAPI and the TPM's guard of the PIN; the Mac's encrypted volume, Keychain, and earlier Panes' paths in `model/mac/`), the files' encryption (`encryption/`), the layer over Chrome's Browsers (`browser/`), the UI (`ui/`), the update check and install (`updates/`), the scenarios' channel (`automation/`), and what a renderer runs (`renderer/`). A header's platform files sit beside it, `<thing>_mac.cc` and `<thing>_win.cc`, and `<thing>_mac.mm` only where an Objective-C class or header leaves no choice (ADR 0124)
- `chromium/` — what Pane does to Chromium
  - `CHROMIUM_VERSION` — the pinned Chromium stable tag
  - `args/` — gn args per build config
  - `patches/` — `series` (apply order), `MANIFEST` (each touched Chromium file → its owning patch), the patches
  - `prebuilt-engines.tsv` — the engine archive each engine key names, and `UPGRADING.md`, the upgrade routine
- `scripts/` — `sync.sh`, `apply-patches.sh`, `build.sh`, `make-patch.sh`, `preflight.sh`, `fetch-crlset.sh`, `package.sh`, `dist.sh`, the checks `check-os-flags.sh` and `compile-windows.sh`, and the prebuilt engine's `engine-key.sh`, `fetch-engine.sh`, `publish-engine.sh`
- `packaging/` — what packaging reads: `VERSION`, the entitlements (`signing/`), the dmg's resources (`dmg/`, with `make-background.swift` and `make-volume-icon.swift`, which make its background and volume icon), and the icon (`icon/`: `AppIcon.icon`, the bundle's, which `package.sh` compiles; the window's renders; and `make-icons.swift`, which makes the icons from them)
- `tests/` — the scenario runner (`run.ts`), fixtures (`pages/`), scenarios that check (`scenarios/`), what measures and never judges (`benchmarks/`), and a held test copy for a pass by hand (`rigs/`)
- `third_party/` — code Pane adapts; `NOTICE` gives origins and licenses
- `docs/` — this guide, `human-checklist.md` (what only a person can do or verify), `qa/`, reports of passes by computer use, `adr/`, one record per subject, `benchmarks/`, measured comparisons of Pane builds with their raw data and how to rerun them, and `growth/`, the media Pane is shown with outside the app
- `evidence/` — gitignored output of acceptance checks

## Chromium checkout

The checkout lives outside the repo, at `~/chromium` (override with `CHROMIUM_ROOT`): `depot_tools/` and `src/`. Keep it out of Spotlight — here `~/chromium` is a symlink to `~/chromium.noindex`, which Spotlight skips; indexing a fresh checkout during a build starved the system into a kernel panic once. It needs full Xcode plus its Metal toolchain (`xcodebuild -downloadComponent MetalToolchain`), an APFS volume, and ~100 GB free.

```bash
scripts/sync.sh            # depot_tools, src at CHROMIUM_VERSION, deps, hooks, //pane link
scripts/apply-patches.sh   # apply patches/series (no-op when unchanged)
scripts/build.sh           # out/Release, target `chrome` (args/release.gn)
```

Every script is safe to re-run. The first sync and the first build take hours; later builds are incremental. The checkout syncs Windows' dependencies and Chrome's PGO profiles too (ADR 0007); to compile for Windows, sync once with `PANE_WIN_TOOLCHAIN_DIR` naming the folder that holds the toolchain's zip, packaged from a Visual Studio install (Chromium's `docs/windows_build_instructions.md`, Packaging), which stays outside the repo. Moving to a newer Chromium is a routine: `chromium/UPGRADING.md`.

## Patches

Edit files in `~/chromium/src`, list each touched file in `patches/MANIFEST` with its owning patch, then regenerate the patch with `scripts/make-patch.sh <patch-name>`. A file belongs to exactly one patch, so a patch is the diff of its own files against the pinned tag.

## Engine unit tests

```bash
bun run test   # scripts/build.sh pane_unittests, then ~/chromium/src/out/Release/pane_unittests
```

`pane_unittests` checks what `//pane` decides without a browser: the model (`pane/model`, with a twin of each of the Swift app's PaneKit tests that its code still has, `docs/benchmarks/panekit-tests.md`; the lock with the PIN's guard on a TPM in memory, `StandInPinGuard`, and the vault on Windows' place, on every system), the Mac's vault and every earlier Pane's data (`pane/model/mac`, with real disk images), Windows' own on Windows (os_crypt's key under DPAPI, read back by Chromium's own reader, and the TPM's guard on the computer's chip: only the right PIN is typed, and `PinGuardWinTest.DISABLED_AWrongPinIsRefused` types a wrong one when asked for by name, `--gtest_also_run_disabled_tests`, in the test VM), the formats every profile file is written in (`pane/encryption`, ADR 0121: the key registry, the SQLite VFS through journals, WAL, kills, Raze and Recovery, and the stream and whole-file codecs), which launch may open which data (`pane/app`, ADR 0012), and the UI's own rules (`pane/ui`: the command table, the Lock's triggers, the design's curves, radii, and colors, the favicons). CI never builds Chromium, so these run locally, before committing an engine change.

```bash
bun run test:asan   # scripts/test-asan.sh: out/ASan, then ~/chromium/src/out/ASan/pane_model_unittests
```

The model's tests run under AddressSanitizer too, with DCHECKs on, as PaneKit's sanitized runs did: `pane_model_unittests` holds `pane/model`'s and the Mac's vault's tests alone, over `//base` and `//crypto`, apart from `pane_unittests`' network stack, built in `out/ASan` from `chromium/args/asan.gn` (release.gn's args with `is_asan` and `dcheck_always_on`, and Chrome's PGO profile off, which was made without DCHECKs). gn generates for that one target, so its first build compiles `//base`, ICU, BoringSSL, and the model, a few minutes at 6 jobs, never the browser. ThreadSanitizer doesn't build on the Mac.

## Scenarios

```bash
bun tests/run.ts --binary <path to browser executable> --out <dir> [--foreground] <scenario or path>...
```

The runner serves `tests/pages` on 127.0.0.1, launches the binary with remote debugging, runs each scenario, and writes `<scenario>.json` plus `report.json` into `--out`. `PANE_SCENARIO_STDERR=1` shows the binary's log. A scenario fails after `--timeout` seconds (600). Whatever it launched is quit, and the directories it took (`context.tempDir`) are removed, with any process still using them. Each entry records whether the screen was locked and the checks it deferred; `report.json`'s `complete` is true only when every check ran.

Scenarios for the Pane app (`lib/pane.ts`) give it throwaway data (`PANE_DATA_DIR`) and a throwaway home (`lib/browser.ts`'s `homeEnvironment`: on the Mac `HOME` and `CFFIXED_USER_HOME`, so its caches, HTTP storage, Downloads, and Crashpad's database are not the user's Pane's; on Windows `PANE_HOME_DIR`, below), and drive it through its automation channel (`PANE_AUTOMATION_DIR`, `pane/automation/`, ADR 0018), which, with every `PANE_` hook, works only under `--use-mock-keychain` (`pane/model/test_hooks.h`), where none of the user's data opens; the engine passes Chromium no switch of a launch without it but Pane's own. A launch whose data lies in the temporary folder, by `PANE_DATA_DIR` or under a home of its own, runs on the mock keychain alone, and the mock keychain only there: either without the other exits with 1 before it reads anything (`pane/app/test_copy.h`, ADR 0012), so a test copy reopened without the switch never reaches the user's Keychain. The channel's commands: JSON commands that call what the UI calls, key presses and clicks posted through the platform's own event path, and a state snapshot with every reply. CDP reads pages; `lib/cua.ts` covers what only the screen shows, and needs an unlocked session and cua-driver's daemon (`canDriveUI()`), or the checks defer. Scenarios run on a Mac in use, so the app never takes the front from the app its user is in: what needs Pane in front (keys a panel takes, a page's fullscreen, Quit Pane?) defers unless `--foreground` allows it, which an idle Mac can, or the user's word (Rigs, below). Preferences are the one thing they still share with the user's Pane: cfprefsd keeps them by bundle ID and ignores a fixed home, and a copy under another bundle ID would ask again for what macOS granted Pane (pasting from other apps), which closes its menus. So a run sets Chromium's `LastRunAppBundlePath` to the build, which the user's Pane resets when it next starts.

**The Mac and Windows.** One runner serves both, and one file of it tests for the OS, `tests/lib/platform.ts`: ADR 0124's rule in the runner's form. What each system does its own way sits in a module that holds both sides and picks one through `bySystem`: `lib/processes.ts` (ps and pgrep; Windows' process table through PowerShell, a process's group as its oldest living ancestor, another process's environment read from its PEB, and `taskkill /F /T`), `lib/tools.ts` (the Swift tools in `tests/tools`, Caps Lock's indicator, a window's capture, the appearance, the volume's disk images, LaunchServices, what makes a stand-in Pane, quarantine or the Mark of the Web and what a download keeps of its page, crash dumps and Crashpad's database, the data folder's privacy (its mode or its ACL) and what an indexer may take of it (Spotlight's or the not-indexed attribute), a byte search of files), `lib/cua.ts` (the screen's lock, `CGSSessionScreenIsLocked` or LogonUI.exe running; the UI driven on the Mac alone), `lib/browser.ts` (the app's home, its folder, the bundle or `chrome.exe`'s, its resources, and URLs handed to it as another app hands them), `lib/windows.ts` (the app's windows on screen), `lib/pasteboard.ts` (the clipboard), and `lib/signposts.ts`. A Windows side with nothing behind it yet throws `<what>: no Windows side yet (<item>, …)`, naming the item of the plan that brings it and what it waits on, so nothing there passes for working. Pane's shortcuts are pressed with `PRIMARY`: ⌘ on the Mac, Ctrl on Windows, as the command table gives them there (W4).

**Where each runs.** On both, unless a scenario says otherwise in its own `export const notOnWindows`: `{ macOnly }`, whose subject Windows doesn't have, or `{ needsDesign }`, which Windows does its own way, not designed yet, each with its reason. There the runner reports it `SKIP`, the reason in its entry's `notRun`; a `needsDesign` one defers all of it too, so the report isn't `complete`. The Mac's alone: ADR 0122's four (`migration`, `identifier-move`, `files-migration`, `files-migration-large`), every row of its table a Mac Pane's data, no Pane having been published for Windows; `applescript` (ADR 0112); `data-volume` (ADR 0113: Windows has no volume, U5); `memory-guarded` (the hardened runtime's refusal: Windows' promise lets a process of the user's read Pane's memory while it is unlocked, U5); and the rig `views-start`. Windows' design to come: `update-install` (W7). A scenario kept off Windows may call the Mac's tools itself; every other, and every module in `lib/`, goes through the modules above.

**On Windows** (the test VM for behavior, a real PC for frames, U6): Bun, PowerShell, and `openssl` (Git for Windows') on the path; the runner started in the console's session, since what starts over SSH shows no window (the VM's `lab/run-interactive.ps1` runs a script there); `--binary <folder>\chrome.exe`, the engine laid out as `mini_installer`'s `chrome.release` names its files; and a printer, which the system's print dialog needs (the VM's own PDF printer won't install, 0x800f0922, so it has one on `FILE:`). The rules above hold there:

- **A test copy** is `PANE_DATA_DIR` in the temporary folder with `--use-mock-keychain`, as on the Mac (`pane/app/data_launch_win.cc`), and its **home** the folder `PANE_HOME_DIR` names in the temporary folder: Chromium reads the user's folders from the shell on Windows, which no variable moves, so a test copy takes them under it (the Desktop, AppData, Documents, Downloads, `PathService`'s overrides). Its data is open once its key is, with no volume (`dataOpen`, `lib/pane.ts`), a lock's close is every Engine key gone (`dataClosed`, the channel's `security.data.keys`), and a profile's own folder is `Engine\<id>` (`profileFolder`). Its secret is the mock keychain's fixed one, as on the Mac.
- **A PIN** is set up through the PIN screen by a launch before the one that checks it (`setPinBeforeLaunch`), no Windows Pane having kept the Electron app's hash, and goes through **the computer's TPM**, as a user's does (ADR 0113): the scenarios' derivation of a guarded key asks the chip for its share, with the PIN (`lib/engine-key.ts`), and lists its keys (`pinGuardKeys`). A wrong PIN counts in the chip's lockout and in Windows' own for a standard user, which refuses every authorization after 4 failures in 480 minutes by default; `security` types 16. So the scenarios that type wrong PINs run in the test VM, whose chip is swtpm's (`scripts/start.sh`; `PANE_VM_TPM=0` boots it with none), with that lockout raised by policy there (`HKLM\SOFTWARE\Policies\Microsoft\TPM`: `StandardUserAuthorizationFailureDuration` 1, `…IndividualThreshold` and `…TotalThreshold` 200), and its state put back from its copy (`tpm-ca/`) when its own count nears 31. Never on a PC whose user works with Windows Hello. `files-encrypted`'s session names its data under that home (`AppData\Local\Pane`), the home its temporary folder (TMP, TEMP), and its audit reads Windows' layout: a profile at `Engine\<id>`, its caches in it.
- **The channel** hands each key, click, move, and scroll to the window's own dispatch, never to the system's input, so a run behind the user's windows works (ADR 0018); `hitTest` asks the window's dispatcher; `nativeWindows` lists the process's windows by role (Views' menus are `MenuHost`, the system's dialogs the sheet or the app-modal panel); `modal` reads Windows 11's print dialog, the shell's, by its frame; `systemEvent` sends Pane's own message window what Windows sends at a display's sleep and a session's lock. The engine reads the computer's time zone alone there, so `changeSystemTimeZone` says it can't, and `fingerprint` defers that check.
- **In front, or behind.** Windows gives keys to the active window alone, and the window's shortcuts run only while it is (`Window::CanHandleAccelerators`), where the Mac's menu bar takes a posted key in the background: a scenario that presses one needs `--foreground` there, which the VM, where no one works, always allows. The VM runs the suite both ways (ADR 0012).
- **Quit** ends a scenario's process as a user's: its launch holds DevTools' keep-alive (remote debugging, no startup window), which Pane's quit lets go (`pane/browser/browser_main_extra_parts_win.cc`).
- **Chrome's words** are title-cased on the Mac alone (Chromium's strings' `use_titlecase`): scenarios compare them in either case (`sameTitle`, `lib/pane.ts`), and the channel chooses a menu's item by its title in either case.
- **In the VM** (2026-11-13, `evidence/views-migration/runs/p5-w3-notes.md`, after W6's `p5-w6b-notes.md`), of the 73 that run on both systems, 54 pass behind and 49 in front, the rest each waiting on another item: **Save history** (ADR 0119): `profile-history` passes there but in about one run in four, where a host the profile visited is still in the loading predictor's database (`Network Action Predictor`) after the clear, a race with Chrome's own writes that the emulated VM's timing shows; **W4**, the platform files and keys: Ctrl in the command table, which `PRIMARY` follows (`find`, `shortcuts`, `quit`, `key-precedence`, `devtools`, `devtools-undocked`, `print`'s Ctrl+P; ⌥⌘I, ⌃⌘L, and an extension's MacCtrl mean nothing there until then), the window's own frame (`window-chrome`, `window-drag`; the system's frame takes 8 points a side, so `sidebar-resize`'s page is 16 narrower and Chrome's extensions page, under 1000 wide, puts `extension-shortcuts`' Keyboard shortcuts in a drawer), the taskbar's icon, with the icon's ten skies, which Windows' build doesn't carry yet (`app-icon`), a second launch's URLs (`open-urls`, and `security`'s handed URL, which it defers), Views' overlays taking a press over the page, which aura gives to the page's own window (`extensions-menu`), and, in front, the page's menu shown (`PageContextMenus::Run`: `context-menu`, `extension-context-menus`), Ctrl's release heard ahead of the page (`sidebar-scroll`, `tab-focus`), and Esc leaving a page's fullscreen, which neither the channel's Esc nor the VM's keyboard's does, the page hearing neither (`fullscreen`); **W5**, what needs Paper: a home for the menu bar's commands (`tab-focus`, `updates`, and what `security` defers behind the lock), and the lock's words on Windows (07.6 shows the Mac's, which tell the user to restore security.json where the chip's key is what is gone; Windows' lockout shakes the dots, its words to come); **W7**, install and update: the CRL set Windows' package carries (`crlset`); **W8**, the audits: the quiet check (`quiet`: Windows' proxy auto-detection asks for `wpad`), and UI Automation, for what `lib/cua.ts` reads on the Mac (`hosted` defers its part) and the print dialog's buttons but Cancel. `certificate-interstitial` timed out once in front and passes alone there. The runner's sides that still throw name theirs: **U5**, no volume; **U6**, frames, footprints, and the trace, a real PC's.

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
scripts/package.sh   # packaging/build/Pane.app: the engine's bundle, Chromium's loader and Pane's UI in its framework, signed
scripts/dist.sh      # packaging/release/Pane-<version>-arm64.dmg: package.sh (stripped), then the dmg
```

For a pass of the edit loop, `~/chromium/src/out/Release/Pane.app` runs as built, without `package.sh`; `security`, `lock-while-open`, and `data-volume` need a package, which carries the volume's watchdog beside the loader.

The app's version lives in `packaging/VERSION`, its only source: the bundle's version keys, Settings, the update check, and the dmg's name all come from it.

## Prebuilt engine

The app packages without a Chromium checkout from a prebuilt engine, as Ardia's does, for a commit whose engine archive is published:

```bash
PANE_ENGINE_OUT="$(scripts/fetch-engine.sh)" scripts/package.sh
```

`chromium/prebuilt-engines.tsv` names the engine key (`engine-key.sh`: the git trees of `CHROMIUM_VERSION`, `args`, `patches`, and `pane`) of the `engine-<key:12>` release that carries `PaneCore-<chromium>-<key:12>.zip`, and its SHA-256. `fetch-engine.sh` downloads and verifies it into `packaging/build/engines.noindex/`, out of Spotlight, which would register its bundle, carrying Chromium's version, as the newest copy of Pane's identifier. A checkout whose engine sources changed has no prebuilt: build the engine. `publish-engine.sh` archives the engine into `packaging/release/`, publishes it as the prerelease, and overwrites that line. Archiving refuses an engine that `build.sh` did not record the current key for (`out/Release/pane-engine.key`): one built before a change to the engine's sources, or from a checkout whose tag, patches, or `//pane` were not this repo's.

## Reading a crash report

A release strips its executables, so a crash report (`~/Library/Logs/DiagnosticReports/Pane-*.ips`) names addresses, not functions. Its symbols are published: PaneCore's, beside the engine archive in the `engine-<key:12>` prerelease (`PaneCore-<chromium>-<key:12>.dSYM.zip`), where `<key>` is the copy's `PaneEngineKey` (`plutil -extract PaneEngineKey raw Pane.app/Contents/Info.plist`). Unzip it, check that its UUID (`dwarfdump --uuid`) is the report's `Pane Framework` image's, and resolve each frame against that image's load address:

```bash
atos -arch arm64 -o "Pane Framework.dSYM/Contents/Resources/DWARF/Pane Framework" -l <load address> <address>...
```
