# The Views build against the last Swift release

The candidate for the first Views release (`docs/plans/views-migration.md`, Phase 4, K2b): `main` at `1f2ea43` with K2b's fixes, a stripped package (`PANE_STRIP=1 scripts/package.sh`, as `dist.sh` makes one, ad hoc, hardened), against the published 0.19.2, the last Swift release, as its dmg shipped it. Measured by Phase 1's budgets, the same way the gate and the shell's end were (`tests/ab.ts`, the two builds taking turns at each benchmark, round after round, the order flipping each round), on an M5 Pro on 2026-11-11, the Mac in use, and on battery from 18:38 on. Then the rest of what "Done when" asks of the Mac. Medians. Raw data: [views-vs-swift.json](views-vs-swift.json). The Views build is the subject throughout and 0.19.2 its baseline.

The Mac slept with its lid closed from 18:38 to 19:34, in the middle of the second round: the rounds or runs that sleep cut are left out, and the benchmarks they took from ran another round after it, so every row has three rounds or more.

Memory at one tab was over its budget. It was taken back after (Memory at one tab, ADR 0125), and the memory rows below are the final tree's, measured again the same way, three rounds from 22:35, on AC power.

## What improved

| | Views | 0.19.2 | Improvement |
| --- | ---: | ---: | --- |
| PIN to first page painted | 431.3 ms | 564.3 ms | 133 ms sooner (−23.6%) |
| No PIN, launch to first page painted | 628.9 ms | 711.1 ms | 82 ms sooner (−11.6%) |
| Launch to first window, cold | 362.5 ms | 399.6 ms | 37 ms sooner (−9.3%) |
| Launch to first window, warm | 375.8 ms | 402.6 ms | 27 ms sooner (−6.7%) |
| ⌘T, key to its commit (background) | 4.0 ms | 25.2 ms | 21 ms sooner |
| ⌘L, key to its commit (background) | 2.4 ms | 14.9 ms | 12.5 ms sooner |
| A tab switch, key to its commit (background) | 4.1 ms | 12.5 ms | 8.4 ms sooner |
| Warm load, `find.html` and `nav-a.html` | 6.0 and 6.4 ms | 9.3 and 9.7 ms | 34 to 36% sooner |
| Warm load, Apple and Wikipedia | 427.5 and 183.3 ms | 754.9 and 195.7 ms | 43% and 6% sooner, by the network as much as the build |
| Memory, 1 tab | 146.1 MB | 149.4 MB | 3.3 MB lighter (−2.2%) |
| The browser process, 1 tab | 58.4 MB | 69.2 MB | 10.8 MB lighter |
| Memory, 10 tabs | 345.8 MB | 352.0 MB | 6.2 MB lighter (−1.8%) |
| The browser process, 10 tabs | 65.6 MB | 80.3 MB | 14.8 MB lighter |

- **Unlock is faster both ways:** the PIN opens the data 133 ms sooner, and a launch without a PIN reaches its page 82 ms sooner, its volume's attach starting 88 ms sooner, over six rounds (three more than the rest, below).
- **Launch** reaches its first window 27 to 37 ms sooner. Both builds launched slower than at the shell's end (400 ms against 268), the Mac in use and on battery.
- **The keys** come to their frame's commit a third to a sixth of the Swift's time. With the screen's own refresh after (`presented`), a key's frame reaches the glass 17 to 19 ms after it.

## What did not improve

| | Views | 0.19.2 | Change | Budget |
| --- | ---: | ---: | --- | --- |
| The GPU process, 1 tab | 32.8 MB | 27.3 MB | +5.6 MB | |
| The GPU process, 10 tabs | 64.8 MB | 58.3 MB | +6.5 MB | |
| Warm load, `smoke.html` | 6.0 ms | 5.8 ms | +0.2 ms (+3.4%) | ≤ +3%: **over** |
| A painted frame reaching the screen | 18.9 ms | 19.0 ms | −0.1 ms | ≤ +1 ms |
| Scroll, frames dropped of 480 | 0 | 0 | the same | ≤ 0.19.2's |
| Warm load, GitHub | 613.8 ms | 619.0 ms | −0.8% | ≤ +3% |

- **The GPU process holds 5.6 MB more at one tab:** the UI's tiles, 4.4 MB of IOSurfaces the browser rasters into on the CPU and the GPU process owns (ADR 0123), and the rest within its runs' swing; 0.19.2's AppKit kept the same pixels in the browser process, 8.2 MB of backing stores. The browser process holds less by more: at one tab the total is 3.3 MB under 0.19.2's (Memory at one tab), where K2b's candidate read 2.5% over, its allocator holding 7.8 MB of copied font tables that the font's own now replace (ADR 0125).
- **`smoke.html`'s warm load** is 0.2 ms later, within the rounds' own swing (0.19.2 5.6 to 5.9 ms, Views 5.9 to 6.4); `find.html` and `nav-a.html` load 34 to 36% sooner on the same rounds.

## Summary

Phase 1's budgets, each row the median of the build's rounds, three or more: launch and pages from the three rounds of every benchmark; unlock from six, three more alone after the first set's round 1 ran as the Mac's load rose to 14; scroll and a frame's way to the screen from three, the round the Mac's sleep cut run again; memory from three rounds of the final tree, ADR 0125's change in it, from 22:35 on AC power (K2b's candidate: 146.9 against 143.3 MB at one tab, +2.5%, and 337.5 against 337.7 at ten).

| Measure | 0.19.2 | Views | Change | Budget | |
|---|---:|---:|---:|---:|---|
| Launch to first window, cold | 399.6 ms | 362.5 ms | -37.1 ms | ≤ +5 ms | ✓ |
| Launch to first window, warm | 402.6 ms | 375.8 ms | -26.8 ms | ≤ +5 ms | ✓ |
| PIN to first page painted | 564.3 ms | 431.3 ms | -133.0 ms | ≤ +25 ms | ✓ |
| No PIN, launch to first page painted | 711.1 ms | 628.9 ms | -82.2 ms | ≤ +25 ms | ✓ |
| Memory, 1 tab | 149.4 MB | 146.1 MB | -2.2% | ≤ +1% | ✓ |
| … the browser process (with the watchdog) | 69.2 MB | 58.4 MB | -10.8 MB | ≤ 0.19.2's | ✓ |
| … the GPU process | 27.3 MB | 32.8 MB | +5.6 MB |  |  |
| Memory, 10 tabs | 352.0 MB | 345.8 MB | -1.8% | ≤ +1% | ✓ |
| … the browser process (with the watchdog) | 80.3 MB | 65.6 MB | -14.8 MB | ≤ 0.19.2's | ✓ |
| … the GPU process | 58.3 MB | 64.8 MB | +6.5 MB |  |  |
| Warm load, `smoke.html` | 5.8 ms | 6.0 ms | +3.4% | ≤ +3% | **over** |
| Warm load, `find.html` | 9.3 ms | 6.0 ms | -35.5% | ≤ +3% | ✓ |
| Warm load, `nav-a.html` | 9.7 ms | 6.4 ms | -34.0% | ≤ +3% | ✓ |
| Warm load, Wikipedia | 195.7 ms | 183.3 ms | -6.3% | ≤ +3% | ✓ |
| Warm load, Apple | 754.9 ms | 427.5 ms | -43.4% | ≤ +3% | ✓ |
| Warm load, GitHub | 619.0 ms | 613.8 ms | -0.8% | ≤ +3% | ✓ |
| Scroll, frames dropped of 480 | 0 | 0 | +0 | ≤ 0.19.2's | ✓ |
| A painted frame reaching the screen, median | 19.0 ms | 18.9 ms | -0.1 ms | ≤ +1 ms | ✓ |
| … p90 | 20.4 ms | 19.9 ms | -0.4 ms |  |  |

The browser process's row counts the volume's watchdog with it, as the benchmark classes every process without a type. One UI edit to a running window, the gate's last row (≤ 30 s), read 34.0 s on battery (`sidebar_button.cc` touched, `chrome` rebuilt at 6 jobs), where B3 read 22.6 s on AC power; on AC power, after the memory rows, it read 24.5 and 24.1 s, twice the same way, the Mac's load at 5.3: within the gate's 30 s.

## Memory at one tab

K2b's candidate read 146.9 MB at one tab against 0.19.2's 143.3, +2.5% against a budget of +1%. Where it went was read from test copies at one tab after the benchmark's 30 s idle, through the runner, in the background (`evidence/views-migration/mem-1tab-probe`): each process's footprint and footprint's categories, vmmap's summary, a detailed memory-infra dump of every process, and the browser's live heap with each sample's stack (Chromium's sampling heap profiler, a sample every 8 KB from launch, read by `Memory.getBrowserSamplingProfile` and symbolized against the unstripped framework); on `out/Release` and the published 0.19.2, taking turns, on battery.

**The browser process.** Its allocator (PartitionAlloc, footprint's App-Specific Tag 14) held 46.0 MB against 0.19.2's 38.0, and CoreGraphics' images 0.5 against 8.2, AppKit's backing stores, which the Views build keeps as tiles in the GPU process. Of the 32.1 MB its live heap held, 7.8 MB were copies of one font's tables: gfx gives each typeface a HarfBuzz face of its own and copies every table the face reads into the heap (`GetFontTable`, `ui/gfx/harfbuzz_font_skia.cc`), and the system font's sizes and weights the UI draws make many typefaces of it, so at launch eleven held a copy each of SF's GPOS (408 KB) and GDEF (270 KB). Nothing else of Pane's own reaches a megabyte. The live heap at one tab, each sample by the first frame from its allocation outward:

| What holds it | MB |
|---|---:|
| SF's GPOS and GDEF, copied for 11 typefaces | 7.8 |
| AppKit and the system's frameworks: the window's NSWindow 1.6, the menu bar 0.5 | 7.4 |
| Chrome's own: prefs 2.7, SQLite 2.0, content 1.9, mojo 1.6, net 1.0, ICU 0.6 | 9.8 |
| ui (menus' models, color providers) | 1.4 |
| Text's runs and glyphs (RenderText 0.8), HarfBuzz's plans (0.7) | 1.5 |
| Skia 1.0, cc's layers 0.7, CoreText 0.2 | 1.9 |
| Accessibility's nodes for every view | 0.8 |
| The rest: extensions 0.3, views' own objects 0.1, and what no frame names | 1.5 |

By the surface whose code is on a sample's stack, the surfaces made at launch hold 2.7 MB of it (below), the sidebar 0.6, the App icon's images 0.7 (the Dock's at 256 and 128 pixels, which its NSImage keeps, and Settings' row), the window's Widget and NSWindow 2.9, and the pages' Browsers 1.9.

**The GPU process.** Its IOSurfaces held 7.4 MB against 0.19.2's 3.0: the page's tile on both, and beside it on the Views build the UI's tiles, 4.4 MB, which the browser rasters on the CPU into IOSurfaces the GPU process owns (ADR 0123). By the view that owns each layer, as cc counts them: the panel frame's four bands 2.0 MB (16 tiles), the toolbar 1.0 (4), the sidebar's header and footer 0.6 (2), its rows 0.5 (1), two icon buttons' 0.03. The rest is 0.19.2's to the megabyte: its allocator (16.3 MB of objects on both), ANGLE's Metal (4.25), the IOAccelerator's (3.7). The window's three output buffers (47 MB) are never drawn into, on either build, and count in no footprint. The Swift kept the same pixels in the browser process, as AppKit's backing stores, 8.2 MB.

**Taken back: the font's own tables** (ADR 0125). `pane/app/engine_switches.cc` turns on Chromium's `HarfBuzzZeroCopyFontTable`, so each HarfBuzz face reads a table where CoreText keeps it. The browser's allocator then holds what 0.19.2's holds (38.0 MB against 38.0), and the browser process is 8.1 MB lighter: 64.1 and 64.9 MB with the tables copied, 56.6 and 56.2 over the font's own, the same build, two launches each, in turn. `EngineSwitchesTest` checks that every launch turns it on. No frame waits on it, the same build with the feature off and on, taking turns, nine launches each, medians:

| From the input to the frame | Tables copied | The font's own |
|---|---:|---:|
| Settings, its first showing after a launch | 39.1 ms (24.0 to its commit) | 39.8 ms (23.9) |
| The App icon picker, first | 31.8 ms (25.3) | 31.6 ms (24.3) |
| Quit Pane?, first | 15.4 ms (8.9) | 13.1 ms (9.2) |
| Find's bar, first | 13.8 ms (6.9) | 11.4 ms (6.7) |
| The switcher, first | 9.8 ms (5.3) | 10.2 ms (5.0) |
| Launch to the first window, cold, three rounds | 391.4, 455.5, 409.9 ms | 390.2, 449.1, 419.4 ms |
| … warm | 397.9, 440.4, 445.6 ms | 393.4, 408.2, 432.9 ms |

It changes no pixel. `surfaces`' 49 renders on the final tree, light and dark, against K2b's of the same tree with the tables copied: 33 in light and 35 in dark the same byte for byte, and the rest apart only where two runs of one build are, as K2b's two light runs were: the update row's clock, the fixtures' port in the address and in a sign-in dialog, the overlay scroll bar's fade, a spinner and a progress bar (each difference's pixels mapped and read). A paired run with the feature off on the same build, through `with-args`, stopped before its first step twice, so the tables' side is K2b's.

**Left for the user.** What `//pane` could take back beyond it costs a first showing: the surfaces it makes at launch, so their first frame makes nothing (ADR 0123), made at their first showing instead. One was measured whole, the profile sheet made at its first showing against the same build as it is, taking turns (two rounds of the probe, six launches each for its first showing): the footprint at one tab 0.1 to 0.2 MB lighter (140.3 against 140.3 MB, the browser process 55.1 against 55.3), within the runs' own swing, and its first showing 5.3 ms later to its commit (4.3 → 9.6 ms) and 6.6 to the screen (19.0 → 25.6). Its objects take 0.89 MB of the sampled heap, but they fill slot spans the allocator holds anyway, so the footprint got back about a fifth of that. For the others, what each allocates (its share of the sampled heap, more than the footprint would get back) and the least its first showing would gain (its layout and text at launch, from a startup trace of three launches, `View::LayoutImmediately` by class, with its making on top, which no trace times):

| Made at its first showing, not at launch | Allocates | Footprint back | First showing slower by |
|---|---:|---:|---:|
| The profile sheet, now made once the first frame is out | 0.89 MB | 0.1 to 0.2 MB (measured) | 5.3 ms to its commit (measured) |
| Settings | 0.67 MB | | at least 2.6 ms |
| The Lock, for a Pane without a PIN (with one, it shows at launch) | 0.58 MB | | at least 1.0 ms |
| Find's bar and the extensions menus, in their cards | 0.45 MB | | at least 1.3 ms |
| Quit Pane?, the switcher, and the full screen's hint | 0.08 MB | | at least 0.7 ms |

Together they allocate 2.7 MB, under 2% of 0.19.2's 149 MB at one tab; if the footprint gets back what it did for the sheet, a fifth of that, about 0.5 MB, for first showings 1 to 5 ms later each, and a refresh later where that pushes a frame past one. With the font's own tables the budget holds without them.

Found and not taken, at no frame's cost: the volume's watchdog reads 2.6 MB where 0.19.2's Swift one read 2.0. The C++ one is linked against `pane/model/mac` whole, so its code reaches AppKit (`NSWorkspace`), LocalAuthentication, CryptoTokenKit, and Security, and 18 libraries load with it, which a narrower dependency may leave out: not tried here.

## The keys, in the background

From the key to the frame, three runs of 30 rounds each, taking turns, the same keys through either build's channel (the cmdt-probe's `ab-key-timing.ts`): the median of the runs' medians (and of their p90s). 0.19.2's intervals end at its commit to Core Animation, so the budget compares the Views build's at the same point; `late=` is when its UI thread heard of the frame, and the screen is when its GPU process predicts the frame reaches it.

| | 0.19.2, to its commit | Views, to its commit | Views, `late=` | Views, to the screen (predicted) | Budget | |
|---|---:|---:|---:|---:|---|---|
| A tab switch (⌘1, ⌘2) | 12.5 ms (14.4) | 4.1 ms (6.3) | 9.7 ms (12.7) | 18.6 ms | ≤ 0.19.2's | ✓ |
| New tab (⌘T) | 25.2 ms (27.6) | 4.0 ms (6.1) | 14.1 ms (17.0) | 18.4 ms | ≤ 0.19.2's | ✓ |
| Address field (⌘L) | 14.9 ms (22.8) | 2.4 ms (4.7) | 8.4 ms (12.3) | 17.0 ms | ≤ 0.19.2's | ✓ |
| A tab woken, to its first paint | — | 46.8 ms (50.7) | 54.5 ms (58.4) | 57.8 ms | | |

The gate measured these with Pane in front (`interaction-timing`, ADR 0012), which waits for a session that may take the front; in the background the keys reach the window through AppKit's queue and Views' own dispatch, the Swift's through its own.

## Nothing that worked broke

Every scenario in `tests/scenarios`, 81, ran on the final tree's build through the runner, on throwaway data, in the background, the screen unlocked throughout, with the rigs (`tests/rigs/views-*`, `update-states`, `update-timing`, and `surfaces`); those that need the volume's watchdog or the hardened runtime on a package (`scripts/package.sh`); those that reach the live Web Store or Google with the network.

| Run | Build | Appearance | Passed |
|---|---|---|---:|
| 67 scenarios and 23 rigs | `out/Release` | light | 90 of 90 (`find` once as the screen locked, `extension-management` once the store's install never landing, then passing) |
| 15 scenarios that render or take the appearance, and 20 rigs | `out/Release` | dark | 35 of 35 (`views-profile-sheet` once "Network changed", then passing) |
| `security`, `lock-while-open`, `data-volume`, `crlset`, `memory-guarded`, `files-encrypted`, `private-traces`, `volume-unindexed`, `engine-keys`, and `views-start` | package | light | 10 of 10 |
| `security`, `data-volume` | package | dark | 2 of 2 |
| `migration`, `identifier-move`, `proxy`, `files-migration-large`, `files-migration` (with the published builds) | package | light | 5 of 5 |

`pane_unittests`: 507 of 507. `pane_model_unittests` under AddressSanitizer with DCHECKs on: 372 of 372, no report. What needs Pane in front was deferred, scenario by scenario, for a session that may take the front.

## No user's data at risk

Each row of ADR 0122's table, on a package of the final tree, with a PIN and without, the earlier builds the published ones (`evidence/views-migration/apps.noindex/published/`):

| Coming from | Without a PIN | With a PIN |
|---|---|---|
| Electron, up to 0.6.0 | `migration`: an Electron app with none opens unlocked, its data moved in, cookie and storage read | `migration`: locked behind its bcrypt PIN at launch, 1234 unlocks, everything moved in and read |
| 0.7.0 to 0.13.1 | `identifier-move`: the former identifier's folders moved, `profiles.json` sealed; `proxy` | `identifier-move`: the same behind a 0.13.1-format bcrypt PIN; `security` |
| A PIN of 0.13.1 or before | — | `security`: the bcrypt hash checked once, the key sealed with the PIN, a damaged `security.json`, the wipe |
| 0.14.0 to 0.17.2 | `files-migration` from 0.15.2 and 0.17.2: converted through kills at 150, 400, 800 ms, everything back, cookies included, 345 files audited, nothing plain; the earlier Pane refused after | `files-migration`: their sessions behind a PIN, locked at launch, converted once unlocked, everything back, 340 files audited, nothing plain, the PIN opening it again |
| 0.18.0 and 0.18.1 | `files-migration` with `PANE_REPAIR_FROM` (0.18.0): what it left put right | the same, the session behind a PIN, each Pane unlocking first |
| 0.18.2 and later | the swap pass: a session this Pane wrote, from a new folder, read whole by 0.19.1 and by 0.19.2 | the swap pass: 0.19.1's and 0.19.2's sessions behind a PIN read here, this Pane's values added, and read back by each |

The swap pass is C1's: 0.19.1, then the Views build, then 0.19.1 again reading back what Views wrote, cookies, local storage, and IndexedDB, and the same with 0.19.2. C1's vectors run in `pane_unittests` (`SwiftVectorsTest`). The published Swift release updating itself to the first Views release, for real, waits for its publishing (K4).

## It looks the same

Every board of the Pane file in Paper, 218, against the Views build's render of its state, light and dark, at 2×, and against the Swift release's render where B2 made one: [views-vs-swift-boards.md](views-vs-swift-boards.md), a row per board. Each surface sits, sizes, and colors as its board in both themes. Against the Swift's renders, the window differs on 0.13 to 0.29% of its pixels on every Lock and PIN state, the empty state, and the update pill; 0.4 to 2.3% on the toolbar, find, the switcher, the dialogs, Settings, the App icon picker, and the extensions menus; 2 to 7% on the profile sheet (its own region 3.2 to 3.7%), and 12 to 18% where Group left it (the user's answer, N1). Nearly all of it is ADR 0060's text. In dark, the dialogs' and Quit Pane?'s shadows fall where the Swift's renders show the page's area white, and the Swift's dialog card let 4% of that white through (`#343436`), where the Views card and Paper's are `#2B2B2D`.

Out of date in Paper, for the user: 04.14 and 04.14.1, the page's menu in the Electron order (ADR 0058's note), and the 33 boards of 09, the extensions pages D3 replaced.

## Nothing is written twice

`scripts/check-os-flags.sh`: no OS flag outside `//pane`'s platform files. Then `pane/`, but its tests, read for a UI, rule, or format kept in two places: every run of 5 and of 8 significant lines (comments, includes, and braces left out) that appears twice, colors and motion outside `pane/ui/design`, and the UI's strings found in two files. Found, and moved to one place, with nothing on the screen changed:

| Kept twice | Now |
|---|---|
| The sidebar's right-click menu, the row held lit under it (`TabRow`, `ProfileRow`) | `RowButton::RunContextMenu` |
| A profile's pages released (`UiApp::UnloadProfile` repeated `ReleasePages`) | `UnloadProfile` calls `ReleasePages` |
| The extension prompts' buttons placed (`ArrangeInstall`, `ArrangeRemove`) | `ExtensionPromptView::ArrangeButtons` |
| Settings' "Pane checks for a new version on its own." (`settings_view.cc`, `ui_app.cc`) | `SettingsUpdate::kChecksOnItsOwn` |
| `UpdateInstaller`'s constructors in both platform files | `updates/update_installer.cc`, shared |
| The channel's `key` reading its modifiers by hand | `Modifiers()`, as `dispatchKey` and `windowKey` |

Read and left, each for a reason: the fingerprint's fields in `pane/model/engine_fingerprint.h` and `pane/renderer/pane_fingerprint.h` are the two ends of `--pane-fingerprint`, across the process boundary, and Blink's platform can't depend on `//pane/model`; the vault's directory walks share an API's idiom, not a rule; "Remove from Pane" and "Pin to toolbar" name one command in each of the two menus ADR 0054 keeps apart; 07.6's and 07.7's lines share their first line, as Paper draws them. Colors come from `pane/ui/design/palette.cc` and the profiles' from `pane/model/profile.cc`, curves and durations from `pane/ui/design/motion.cc`, and only the platform logos draw their own.

## The measures

By the plan's table's commands, on the final tree (`main` at `1f2ea43` with K2b's changes):

| Measure | At 0.19.1 (`8bee4e5`) | Now | Target at the Mac's cutover | |
|---|---:|---:|---:|---|
| Swift in the product | 22,924 | 0 | 0 | ✓ |
| Objective-C++ (`.mm`) | 7,929 | 1,855 (18 files) | ≤ 800 | missed (K2a: what stays calls an Objective-C class or header) |
| Product lines: `pane/`'s `.cc`, `.mm`, `.h`, and C | 41,578 | 85,487 | ≤ 37,000 (estimate) | missed: 14,225 of them `pane/`'s unit tests and their helpers, 3,707 the scenarios' channel, which the 0.19.1 column never counted, and Views is wordier than SwiftUI |
| Chromium files the patches edit | 93 | 90 | ≤ 89 | missed by 1 (reading mode's hunk, K3) |
| Lines changed in those files | 1,034 | 994 | ≤ 990 | missed by 4 |
| Files the patches add | 9 | 9 | ≤ 9 | ✓ |
| Build systems | gn, xcodebuild, SwiftPM | gn | gn | ✓ |
| Tests | 284 PaneKit, 63 `pane_unittests`, 81 scenarios | 0, 507, 81 | the same cases, all in `pane_unittests`, and 81 | ✓ ([panekit-tests.md](panekit-tests.md)) |

## Found on the way, and fixed

- **The scenarios' channel had no AppleScript.** `applescript` failed on Views ("unknown command appleScript"): the Swift's channel ran a script in the app against itself, and the Views channel never got the command, so no phase's list held the scenario. `appleScript { source }` runs it now (`pane/automation/system.h`, `NSAppleScript` in `system_mac.mm`), and the scenario passes: the dictionary Chromium keeps (ADR 0112) answers for Pane's window, its active tab, and the shown profile's tabs. The switch to another profile needs Pane in front.
- **A page's click could land before the page's first frame.** `surfaces` failed 3 times in 5 at Open external app's prompt: the press, sent through DevTools the moment the page loaded, reached nothing of it, its own click listener never called, the input router having no frame of the page yet to find a target in. `clickAt` (`tests/lib/traced.ts`) waits for two of the page's animation frames first, as a person clicks what they see: 3 of 3 after, and every caller passes a page's own DevTools session.
- **New stayed lit under the profile sheet's scrim in every Views render of it,** where Paper's boards and the Swift's renders show it plain: a press with no move before it left the window without a pointer to take away. `profile-sheet` moves to New before pressing it, as a hand does, and the renders match. With the real pointer the first move clears it, as AppKit's tracking areas did.
- **ADR 0122's rows with a PIN and without.** Each data scenario had one: `migration` runs from an Electron app with no PIN too, `identifier-move` behind a 0.13.1-format bcrypt PIN too, and `files-migration` converts 0.15.2's and 0.17.2's sessions behind a PIN, and repairs 0.18.0's damage behind one, beside the passes it had.
- **Six things written twice,** each now in one place (above).

## Caveats

- **The Mac was in use.** Its user's apps ran throughout, one of them a renderer at a full core, and another session's iOS Simulator with its test runner: the one-minute load is in the raw data, sampled every 30 s. Taking turns puts a trend on both builds alike; it doesn't remove the noise, which is largest in what a real site's network decides.
- **Both builds stripped and signed, neither notarized:** 0.19.2 as its dmg shipped it, the candidate as `dist.sh` packages one (`PANE_STRIP=1`), ad hoc, with the hardened runtime.
- **The keys are timed in the background.** With Pane behind the app in use, a key reaches the window through AppKit's queue and Views' own dispatch (ADR 0012): the Swift's intervals end at its commit to Core Animation, the Views build's at the same point (`committed`), and at the screen as its GPU process predicts it (`presented`). The gate's own measure takes the front (`interaction-timing`), which waits for a session that can take it.
- **Renders leave the page out,** in both builds: the page's own pixels are Chromium's, and the A/B's page loads measure them.
- **Memory's rows ran again on AC power,** three rounds from 22:35, the final tree against 0.19.2. A run from 21:36, on battery, lost its candidate's first two rounds to the Mac: the screen locked under the first, and the Mac slept with its lid closed from 21:55 to 22:30 under the second. It is left out (its one 0.19.2 round read 151.2 MB at one tab, these three 149.4 by their median).

## Rerunning

The budgets, a stripped package of the candidate against the published 0.19.2, taking turns:

```bash
PANE_STRIP=1 PANE_APP_OUT=<dir>/Pane.app scripts/package.sh
bun tests/ab.ts --base <0.19.2>/Contents/MacOS/Pane --candidate <dir>/Pane.app/Contents/MacOS/Pane \
	--out <out> --rounds 3 launch unlock pages memory scroll frame-latency
```

The keys in the background, on either build, through its own channel (`evidence/views-migration/cmdt-probe/ab-key-timing.ts`, kept with the evidence), three runs each, taking turns; and with Pane in front, as the gate measured them, once the Mac's user allows it:

```bash
bun tests/ab.ts --base <0.19.2> --candidate <Views> --out <out> --rounds 5 interaction-timing
```

The renders, light and dark, from the rigs (`tests/rigs/surfaces.ts` and `views-*.ts`, `--appearance light|dark`), and the boards from Paper's local MCP (`export`, 2×, read only).

Where the memory goes at one tab, from a test copy through the runner (`evidence/views-migration/mem-1tab-probe/probe.ts`, kept with the evidence): footprint by process and its categories, vmmap, a detailed memory-infra dump of every process, and with `PROBE_HEAP` the browser's live heap with its stacks, from Chromium's sampling heap profiler started at launch; each surface's first showing after a launch (`first-showings.ts`); a feature turned off on the same build for an A/B (`PROBE_ARGS`):

```bash
PROBE_LABEL=views PROBE_HEAP=1 PROBE_ARGS="--enable-features=HeapProfilerReporting:stable-probability/1/browser-sampling-rate-bytes/8192" \
	bun tests/run.ts --binary <Views build> --out <dir> evidence/views-migration/mem-1tab-probe/probe.ts
PROBE_ARGS="--disable-features=HarfBuzzZeroCopyFontTable" PROBE_LAUNCHES=3 \
	bun tests/run.ts --binary <Views build> --out <dir> evidence/views-migration/mem-1tab-probe/first-showings.ts
```
