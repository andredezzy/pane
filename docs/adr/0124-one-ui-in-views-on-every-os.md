# Pane's UI is one, in C++ Views inside its own Chromium, for the Mac and Windows, with no Swift

Pane's interface exists once, in C++ with Views, Chromium's own toolkit, in the `//pane` tree that Chromium's build links into Pane's framework: every window, sidebar, sheet, dialog, and panel, one code path that the Mac runs and Windows compiles. It calls Chrome's `chrome/browser` directly, as Chrome's own UI does, with no bridge and no second language. It replaced the Swift app (AppKit and SwiftUI) that embedded the engine through an Objective-C API from 0.7.0 to 0.19.2 (ADR 0004). The user's calls, from 2026-11-07 and 2026-11-08, by `docs/plans/views-migration.md`, which carried them out:

- **U0. Views, one UI on both OSes, no duplicated code, and the Swift goes.** The user asked for Aside's pattern, a whole migration rather than a second UI for Windows, as simple as it can be, every feature kept, and performance kept in full and improved where it can be.
- **U1. Every profile stays in one window, over hidden Browsers** (ADR 0014): each Pane window is one `views::Widget` of Pane's, and every profile shown in it has a hidden `Browser` behind it with a `HostedBrowserWindow`, no `BrowserView`. It keeps Pane's sidebar, which shows every profile at once, and it already ran under the Swift app. It is where Pane differs from Aside, which overwrites `Browser::profile_` in place and shows one profile at a time.
- **U2. Chrome's stock pieces where Pane's look allows,** each drawn in Paper first: the page's context menu (`RenderViewContextMenu`, native on the Mac), the file panels, and Chrome's own extensions page and its shortcuts page in the profile's tab (D3, ADR 0062). Pane's look stays for what an ADR chose on purpose: the dialogs (0021), find bar (0067), extensions menu (0054), install dialog (0056), sad tab (0027), fullscreen hint (0025), Quit Pane? (0053), the progress bar (0081), and the extension action's popup (the user's answer on Paper 10.5). Menus stay the system's, through `views::MenuRunner`.
- **U3. The Swift UI froze once the prototype passed its gate** (2026-11-09): data-loss and security fixes only, until the cutover, so nothing was written twice.
- **U4. Release builds stay local, as engines were.** This Mac builds the engine, Pane's UI with it, and `scripts/publish-engine.sh` uploads its archive; CI packages, signs, and attests it, and keeps lint, typecheck, knip, and the OS-flag check (ADR 0049). The engine's unit tests and the scenarios run here before each push and tag. Self-hosted or rented builders can come later.
- **U5. Windows gets its own promise, resting on the PIN** (taken at Windows' turn): no encrypted volume there, since one needs elevation, so names, sizes, and times show at rest; ADR 0121's Engine keys cover the contents. The README and Settings say so then.
- **U6. Windows needs a real x64 PC, or a rented one with a GPU, for frame numbers, and a certificate or the Store for a public release** (taken at Windows' turn); an alpha can go unsigned to testers, built by the Mac's cross build.
- **U7, as D2 changed it. The tree moved first, in one commit, before any new file:** `engine/pane` to `pane/`, the rest of `engine/` to `chromium/` (ADR 0086).
- **U8. Windows after the Mac's cutover,** compiling from the prototype on, so nothing Mac-only creeps in.
- **D1. `main` became the Views line at once.** The Swift line's fixes ship from a `swift` branch cut from `v0.19.1` (engine archive `46b8f8b35c24`), which `release.yml`'s tag check accepts, until `main`'s first release (AGENTS.md). The App workflow and its setup action went.
- **D3. Chrome's own extensions page and shortcuts page replace Pane's** (ADR 0062), with every function kept: search, details, site access, options, remove, on and off, pin, and shortcuts. The plan's author had recommended keeping Pane's.
- **D4. An engine built with Chrome's PGO profile, measured against 0.19.1, shipped as 0.19.2** once the user saw the numbers (`docs/benchmarks/pgo-vs-0.19.1.json`), and became the baseline the Views build was measured against.

## Why

- **Windows.** Dia, the closest native Swift browser, took about 2 s to its window on Windows, launched through the shell, where Chrome took 490 ms, and loaded 52 non-system modules to Chrome's 5, with WinUI under every layer over the page; Aside, in Views, took 350 ms launched directly, to Chrome's 290, and draws in one surface (a Windows 11 VM, 2026-11-07, no GPU). A Swift shell on Windows means WinUI and a second UI.
- **One UI.** With the UI in the engine, nothing is written twice, the bridge goes, and so do a language, a project generator, and a package manager: Pane builds with gn alone.
- **It costs nothing on the Mac.** Against 0.19.2 on one Mac, the Views shell launches 13.8 ms sooner cold, reaches the first page 137.5 ms sooner after the PIN, and holds 1.7% less memory at one tab and 2.4% at ten, its browser process 8.5 and 13.1 MB lighter (`docs/benchmarks/views-shell.md`); a tab switch, ⌘T, and ⌘L hold their baselines (the prototype's gate, 2026-11-09).
- **The cost is written down.** Pane's UI calls `chrome/browser` directly, so each Chromium upgrade touches more of `//pane` (`chromium/UPGRADING.md` counts it). Views is wordier than SwiftUI and the model's port is C++, so the product's lines grew, from 41,578 at 0.19.1 to 84,169 at the cutover, 12,795 of them the unit tests the model brought from PaneKit, which the Swift's count never had, and 3,434 the scenarios' channel; what fell is the layers, the languages, and the build systems.

## The rules it keeps

- **One code path.** A platform's own code goes in a header's platform files, `<thing>_mac.mm` and `<thing>_win.cc` beside `<thing>.h`, which hold no logic two of them would share, and in `BUILD.gn`; no other file tests for the OS (`BUILDFLAG(IS_MAC)`, `IS_WIN`, or the compilers' own macros). Objective-C++ goes only where the OS leaves no choice.
- **Two targets that stay apart from the browser.** `pane/model` and `pane/encryption` depend on neither `//content`, `//ui/views`, nor `//chrome`, so their tests run without a browser; everything else is the one `//pane` target, which calls `chrome/browser` directly. `gn check` holds both.
- **The engine stays as faithful to Chromium as it can be** (ADR 0004): no edits in `chrome/browser/ui/views/` while `//pane` or a seam can do it, and no `chromium_src` overrides, which are edits in disguise.
- **No new feature and no redesign during the migration,** and no change of a data format: every Pane's data reads as before (ADR 0122).

## What was rejected

- **Keeping Swift on the Mac and adding a Windows UI:** two UIs, which the user asked to end.
- **Aside's in-place profile swap:** it edits `browser.{h,cc}`, `tab_strip_model.*`, and every surface caching a profile, and Aside's changelog lists at least ten fixes for state left stale after a switch.
- **One `BrowserWidget` per profile, swapped in place:** each switch hides one native window and shows another, with activation, shadow, z-order, Spaces, and Snap to get right. **Several `BrowserView`s in one widget:** `kBrowserViewKey` and `Profile::kProfileKey` are single properties of a window, so every lookup gets whichever was set last.
- **WinUI, WinRT, a C API, or any bridge:** the measures that moved this decision are what those cost.
- **A switch keeping both UIs on `main`** (D1's alternative): a gn arg, an engine key in two modes, an extra engine publish, and every hook written twice behind a flag.
- **Pane's own extensions page, kept** (D3's alternative): about 1,000 lines of Swift and engine code that copied Chrome's.
- **A component build for the edit loop:** one edit in `//pane` rebuilt `out/Release`'s `chrome` in 22.6 s at 6 jobs, inside the gate's 30.

## How it is checked

- **No Swift, no Objective-C API, no Xcode project:** `git ls-files '*.swift'` names only the tools that make the icons and the dmg's art (`app/icon`, `app/dmg`), the scenario runner's probes (`tests/tools`), and the media's (`docs/growth`), none of them shipped; `pane/public` and `framework.exports`' additions are gone, so the framework exports Chrome's entry points alone, which Chromium's own order check verifies at every build (ADR 0009).
- **One code path:** `scripts/check-os-flags.sh` fails on a test of the OS in a shared file of `//pane`, in CI (`gates.yml`) and before each commit; `scripts/compile-windows.sh` compiles the shared code for Windows x64, and `gn check out/Win '//pane/*'` checks its includes, before each push of an engine change (AGENTS.md).
- **Nothing that worked broke:** the scenarios (`tests/scenarios`), on the Views build, and `pane_unittests`, which holds a twin of each of the Swift app's PaneKit tests whose code Pane still has; the map says, test by test, what checks the rest (`docs/benchmarks/panekit-tests.md`).
- **Nothing got slower:** the budgets against the last Swift release, measured the same way on one Mac (`docs/benchmarks/views-shell.md`), and again on the release candidate before it ships (`docs/plans/views-migration.md`, K2).
- **Every published Pane updates to it with its data:** ADR 0122's table, row by row, against the Views build, and the real update from published builds once it is released (ADR 0092).
