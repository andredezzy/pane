# Plan: one UI in Chromium Views, on Mac and Windows, with no Swift left

Status: written 2026-11-07, and the user took its decisions the same day. Checked again at `8bee4e5` (0.19.1) on 2026-11-08, then simplified the same day by seven readers (the log), which cut its phases from twelve to six, and the user answered D1 to D4 the same day. Phase 0 is next. Written from four read-only reports on the code at `989d176`, static reading of Aside's and Dia's packages, and measurements of them on Windows in a VM. Nothing in it has been built or run. Its last commit removes it (ADR 0086); what it decides lives in the ADRs.

## The goal

The user's request (2026-11-07): move to Aside's pattern, the UI in C++ Views, with no duplicated code: a whole migration, not a second UI for Windows, simplifying as far as it goes, keeping everything working as well as it can, with the best organization possible. On 2026-11-08 the user added: as little complexity as possible, every feature kept, performance kept in full and improved wherever it can be.

So Pane ends up as:

1. **One UI.** Every surface exists once, in C++ Views, inside the Chromium build, and the same code runs on Mac and Windows.
2. **One language.** No Swift in the product, no Objective-C API, and Objective-C++ only where the OS leaves no choice.
3. **No bridge.** The UI calls Chromium directly, as Chromium's own UI does.
4. **Windows as a second set of platform files,** not a port.

Users on the Mac see no change, except where this plan says so and the user approved it.

### Done when

On a release build, on the Mac first and then on Windows:

1. **Nothing that worked broke.** All 81 scenarios pass on the Views UI, or the log names where a check went. Every PaneKit test exists as a `pane_unittests` case, and the `engine-keys`, `files-encrypted`, and disk checks pass (memory `check-claims-by-grepping-disk`).
2. **No user's data is at risk.** Every published Pane updates straight to the first Views release with its data, as ADR 0122 promises: each row of its table passes its scenario against the Views build, with and without a PIN, and 0.19.1 reads back what the Views build wrote (C1's swap pass). The published Swift release updates itself to the Views release, for real (memory `update-path-e2e-check`).
3. **Nothing got slower.** The budgets in Phase 1 hold against the last Swift release, on one Mac, measured the same way. Results go in `docs/benchmarks/`.
4. **It looks the same.** Every surface, in every state, in light and dark, at 2×, matches its Paper board and the last Swift release's render, except what the user approved changing.
5. **Nothing is written twice.** Outside the platform files this plan names, no UI, rule, or format exists in two places, and no shared file tests for the OS.
6. **Complexity is measurably down,** by the table below, and the repository reads clean: README, AGENTS.md, DESIGN.md, CONTEXT.md, `docs/building.md`, and the ADRs describe what exists.
7. **Tried as a person would,** by the rules below, in light and dark, with VoiceOver on the Mac and Narrator on Windows.

## How to use this plan

- **Every claim is a lead.** `[measured]` was observed running. `[read]` means this plan's author checked the source. `[review]` means only a reviewer read it. Nothing here compiled. A claim that proves wrong goes in the log with what was found.
- **Chromium first.** Before writing anything, look for what `//base`, `//ui/views`, `//components`, and `//chrome/browser` already do: most of this plan's cuts came from there.
- **Port behavior, not code.** Read the Swift, its ADR, and its scenario, then write what Views wants. Line-for-line translation carries the bridge along.
- **A surface is done when** its scenarios pass on Views, its renders match, and its Swift and bridge are deleted, in the same commit.
- **Line numbers** are at `8bee4e5` for Pane's files in Phase 0, Phase 2, and the counts, and at `989d176` elsewhere; Chromium's are at 158.0.8127.18. They drift; the names don't.
- **Paths.** Bare Swift files are under `app/Pane/` or `app/PaneKit/Sources/PaneKit/`. `CR/` is `~/chromium/src`. Other bare `.cc`, `.mm`, and `.h` files are under `pane/` (`engine/pane/` until B7) or, when it has none, `CR/`. `frame/` is `CR/chrome/browser/ui/views/frame/`. Bare scenario names are in `tests/scenarios/`.
- **Targets check the work.** A target is never a reason to drop a guarantee: miss it and say why.

## Where things stand

### Measured at `8bee4e5` (0.19.1)

| Measure | At `989d176` | At `8bee4e5` | Target at the Mac's cutover |
|---|---:|---:|---:|
| Swift in the product (`app/Pane`, PaneKit's sources, `VolumeWatch`) | 20,947 | 22,924 | 0 |
| Objective-C++ (`.mm`) | 8,052 | 7,929 | ≤ 800 |
| Product lines: that Swift, `engine/pane`'s `.cc`, `.mm`, and `.h`, and C | about 36,100 | 41,578 | ≤ 37,000 (estimate) |
| Chromium files the patches edit | 123 | 93 | ≤ 89, plus ≤ 7 for Windows |
| Lines changed in those files | 1,379 | 1,034 | ≤ 990 |
| Files the patches add | 11 | 9 | ≤ 9 |
| Build systems | gn, xcodebuild, SwiftPM | the same | gn |
| Tests | 248 PaneKit, 64 `pane_unittests`, 77 scenarios | 284, 63, 81 | the same cases, all in `pane_unittests`, and 81 |

The first column came from the reports this plan was written from; the second from the commands below. Between them, the review plan kept every upgrade path (ADR 0122) and took 30 files out of the patches (its E6, E7, E9, E12, E14, and E15). The patch targets count only what the Views UI still removes: `framework.exports` with `chrome/BUILD.gn`'s check beside it, `extension_action_api.cc`, and two seams ("What goes"). With Chrome's stock dialogs for external apps and extension installs, which only the user can choose in Paper, they'd be ≤ 87 and ≤ 960.

Be honest about what this buys. Lines fall little, since Views is wordier than SwiftUI, and 89 of the 93 edited files stay: encryption's 42, the fingerprint's 22, history's 8, and the seams for hosted Browsers and quiet mode. What falls is the number of layers, languages, and build systems, and what doubles when Windows arrives: nothing does. Measure again with these, and put before and after in the log:

```bash
git ls-files -z 'app/Pane/*.swift' 'app/PaneKit/Sources/*.swift' 'app/VolumeWatch/*.swift' | xargs -0 cat | wc -l
git ls-files -z '*.mm' | xargs -0 cat | wc -l
git ls-files -z 'pane/*.cc' 'pane/*.mm' 'pane/*.h' '*.c' | xargs -0 cat | wc -l   # plus the Swift: product lines
awk 'function end(){if(f){if(n){N++}else{E++;L+=c}}c=0;n=0;f=1} /^diff --git /{end();next} /^new file mode/{n=1;next} /^(\+\+\+|---) /{next} /^[-+]/{c++} END{end();print E" Chromium files edited, "L" lines changed, "N" files added"}' chromium/patches/*.patch
grep -rhoE '@Test\b' app/PaneKit/Tests | wc -l; git grep -hE '^(TEST|TEST_F|TEST_P)\(' -- pane | wc -l; /bin/ls tests/scenarios/*.ts | wc -l
```

### What was found on Windows `[measured]`

In the VM (memory `windows-test-vm`): arm64, no GPU, logged in, one local page, on 2026-11-07. A launch is the median of runs 2 to 4, warm, to a visible main window, not first paint.

| | Launch | Browser process, 10 tabs | Page renderers, 10 tabs | Total | Non-system modules |
|---|---:|---:|---:|---:|---:|
| Chrome | 290 ms (490 through the shell) | 47 MB | 177 MB | 245 MB | 5 |
| Aside | 350 ms | 164 MB | 755 MB | 957 MB | 11 to 16 |
| Dia | 2,000 ms, through the shell only | 150 MB | 147 MB | 312 MB | 52 |

Aside's memory comes from a signed-in account with synced profiles, and 428 MB of that total is 10 extension processes, so it says nothing clean about its path. No frame or input numbers exist: those need a real x64 PC.

### What Aside does, and what this plan takes from it

Aside 1.0.1022.1 (Chromium 154), from its binary and changelog. `[read]` for the strings, `[review]` for the disassembly. Its UI is C++ Views on a real `BrowserView`, with `aside_*` files in `chrome/browser/ui/views/`; on Windows it is one `Chrome_WidgetWin_1`, with sidebar, toolbar, and cards over the page in one surface `[measured]`, and WinUI loaded only for a backdrop, a caption overlay, and a popover. It switches profiles by overwriting `Browser::profile_` in place, shows one profile's tabs at a time, and its changelog lists at least ten fixes for state left stale after a switch.

| Take | Leave |
|---|---|
| The UI in Views, in the Chromium build, one code path on every OS | Overwriting `Browser::profile_`: it edits `browser.{h,cc}`, `tab_strip_model.*`, and every surface caching a profile, and shows one profile at a time where Pane's sidebar keeps them all (U1) |
| One surface: overlays are plain views over the page | Edits inside `chrome/browser/ui/views/frame/`: Pane's code goes in `//pane` |
| Stock Chromium pieces where Pane's look allows | WinUI: Pane's UI has no material to reproduce |
| Chromium's vertical tabs as a reference for reorder | Omaha and its installer: Pane keeps its update check (ADR 0002) |

### The rule this plan keeps

**The engine stays as faithful to Chromium as it can be** (the user, 2026-11-06, ADR 0004): a change goes in `//pane` first, through a seam Chromium already has second, and into a Chromium file only when neither can do it. The UI calls `chrome/browser` directly, as Chromium's own UI does, and ADR 0004's count of what the patches edit measures the cost at each upgrade.

## The architecture

### The window

**Each Pane window is one `views::Widget` of Pane's, and every profile shown in it has a hidden `Browser` behind it (U1, design ii).** This is what `WindowHost` and `HostedBrowserWindow` already do (ADR 0014); only what's drawn in the Widget changes.

| Design | What breaks |
|---|---|
| (i) One `BrowserWidget` per profile, swapped in place | Each switch hides one native window and shows another: activation, shadow, z-order, Mac fullscreen Spaces, Windows snap (inference). |
| **(ii) Pane's Widget, with hidden Browsers** | `GetBrowserViewForBrowser` returns null (158 call sites outside tests), as it does today. Needs Pane's frame on each OS. |
| (iii) One `BrowserWidget`, several `BrowserView`s | `kBrowserViewKey` and `Profile::kProfileKey` are single window properties (`frame/browser_view.cc:1209,5040,5044`), so every lookup gets whichever was set last. |
| (iv) Aside's | See above. |

(ii) already runs, leaves `browser_view.cc` unedited, and Chromium builds the same seam for itself: `WebUIBrowserWindow`, a non-`BrowserView` window on `views::Widget`, branched at `frame/browser_window_factory.cc:47` `[read]`.

**What must hold** `[review]`: no Browser in a shared Widget has a `BrowserView`. One active Browser per Widget. Pane owns its view tree and tab model. One `FocusManager`, so `pane::ExtensionShortcuts` stays, since a per-Browser `ExtensionKeybindingRegistryViews` would fire background profiles. ADR 0017's no-reentrancy rule stays, for UI code inside tab-strip notifications.

**Pages:** one `views::WebView` per window, given each tab's `WebContents` with `SetWebContents`. On the Mac, `RenderWidgetHostViewMac::SetParentUiLayer` draws the page in the Widget's compositor, so views composite over it, and a window runs one compositor where today it runs two and builds one at each switch (`window_host.mm:149-155`, `web_contents.mm:100-102`) `[review]`. That ends the `NativeViewHost` that has held the Swift app's view since the review plan's E7 (`18ba113`).

**Corners:** layer radii only, `SetNativeViewCornerRadii` or `Layer::SetRoundedCornerRadius` with `SetIsFastRoundedCorner`, at 18 × `circularEquivalent`, as `PageHostView.swift:21-27` does today `[read]`. Never `View::SetClipPath` on a layer that holds a page or video: it installs a mask, and with it an offscreen pass every frame (`CR/ui/views/view.cc:783-787,3888-3891`) `[review]`.

**A window is found from its Browser,** registered with `ui::ScopedUnownedUserData<HostedBrowserWindow>` as `SidePanelUIBase` registers itself, in place of the process-wide `LiveWindows()` and `Records()` with their manual `Forget` (`browser_window.mm:308-343`) `[read]`, where ADR 0014's stale-Browser bug came from.

### The tree

```
pane/              //pane: one target, the UI in it, as today
  app/             the main delegate: start (C5), the argv filter, the feature list
  model/           PaneKit's port, rules and data: //base, //crypto, //net only
    mac/           ADR 0122's paths, the volume, DataLock, the watchdog: built on the Mac only
  encryption/      as it is
  browser/         hidden Browsers, profile contexts, tabs, network, fingerprint, history
  ui/              design/, window/, sidebar/, toolbar/, page/, panels/, sheets/, dialogs/, extensions/, lock/
  updates/  automation/  renderer/  common/
chromium/          what Pane does to Chromium: version, args, patches, UPGRADING.md
packaging/         icons, dmg, installer, signing
scripts/  tests/  docs/  third_party/
VERSION
```

**Two rules, and the build enforces them.** `model` and `encryption` are gn targets of their own whose deps leave out `//content`, `//ui/views`, and `//chrome`, and `gn check` runs before each commit. Everything else stays in the one `//pane` target and calls `chrome/browser` directly, since what the UI must implement lives there (`ExtensionsContainer`, `ExtensionPopup`, `FindBarController`, `DevtoolsUIController`, `TabStripModelObserver`): a layer between them would rebuild the bridge in C++. No `DEPS` files: gn ignores them, and only presubmit's checkdeps reads them (`CR/PRESUBMIT.py:3839`) `[review]`. The tree moves first, at Phase 0, before any new file (B7, D2).

### One code path

- **A platform file holds no logic.** It implements one header, named `<thing>_mac.mm` or `<thing>_win.cc` beside `<thing>.h`, as Chromium does. If two platform files share a line of logic, it moves up.
- **`BUILDFLAG(IS_MAC)` and `IS_WIN` appear only in `BUILD.gn`, `_mac.mm`, and `_win.cc` files,** which a grep checks before each commit. Copy that names the OS or a shortcut comes from one function.

| Header | Mac | Windows |
|---|---|---|
| Secret store | Chromium's `KeychainPassword` and `KeychainV2`: the same item, named by BRANDING | os_crypt's DPAPI (U5) |
| Volume, DataLock, watchdog, ADR 0122's paths | `model/mac` | None (U5): no Pane ever ran there |
| Frame | One `NonClientHitTest` for both (`HTCAPTION` drags on the Mac too, `CR/ui/views/cocoa/native_widget_mac_ns_window_host.mm:1461-1472`); the lights at (12, 12) | Caption buttons; `HTMAXBUTTON` gives Snap Layouts |
| Menus | One `ui::MenuModel` per menu: the menu bar through `MenuControllerCocoa`, the rest through `views::MenuRunner`, which shows an NSMenu | `views::MenuRunner`; the menu bar's commands need a home (Paper) |
| App icon | Dock (ADR 0089) | `Widget::UpdateWindowIcon`, `.ico` |
| Update install | Dmg swap at quit | W7 |
| Lock triggers | `ui::AddScreenLockCallback`, `ui::CalculateIdleTime`, `base::PowerMonitor`, and one small pair for display sleep and session resign | The same |
| Process start | Chromium's loader and BRANDING | The same, with Chromium's install modes branded (W4) |

Everything else is one implementation: `views::Textfield` and the rest of the controls, `net::TCPServerSocket` for the SOCKS relay, `base::FilePathWatcher` for the channel.

### What goes, and what Chromium already does

- **The bridge, about 4,100 lines of `//pane`**, counted at `8bee4e5`: `public/` (1,241), `ui/dialogs` (665), `extension_objects` and `extensions_internal.h` (497), `web_contents` (520), `extension_menu` (488), `action_popup` (295), `ui/select_file_dialog` (212), `browser_window_api` (152), `app_callback` (55). `profile_migration` (2,014) stays: it is the 0.14 to 0.17.2 conversion ADR 0122 keeps, and C++ already.
- **What never gets ported from Swift:** `DispatchQueue.main` ×16, `assumeIsolated` ×19, `Task.detached` ×15, `pendingTabID`; the mirrors at `BrowserModel:101-107`, `ExtensionsController:22`, `actionAnchors`, `KeyPress`; `KeyRouting` and `KeyRouter` (488 lines, with `KeyOverlays`' 13 flags) `[read]`, since in Views the topmost overlay holds focus, Esc and Return are its dialog's accelerators, the command table is `ui::Accelerator`s on the window's `FocusManager`, and page first or app first is `PreHandleKeyboardEvent` and `HandleKeyboardEvent`; the AppKit fights: `TitleBar`'s swizzle, `WindowDragArea`, `textField(prompting:)`, `selectAddressIfOwed`, `Settled`.
- **`AfterFrame` is kept, as behavior:** it makes a new or sleeping tab's page on the turn after the frame that answers the input, and hides the tab left after it, which saves 9.7 ms per tab switch, 15.4 ms per new tab, and 9.0 ms per address focus `[measured, 0.13.1]` (`docs/benchmarks/interactions.md:9-11`). About 20 lines on `ui::CompositorObserver::OnCompositingDidCommit` and a posted task (H5).
- **What Chromium already has:**
  - `ClientHints.brandList` (`embedder_support::GenerateBrandVersionList`), `ToolbarProgress`'s `UnitBezier` and `Fade` (`gfx::CubicBezier`), most of `ExtensionShortcut` (`extensions::Command`, `ui::Accelerator`), `Versions` (`base::Version`), `BundleIdentifier`'s names (BRANDING).
  - `SafeStorage` and the proxy passwords' Keychain code, about 320 lines: `KeychainPassword::GetPassword()` makes the secret as `SafeStorage` does, `KeychainV2::FindGenericPassword` finds one where none may be made (`CR/components/os_crypt/common/keychain_password_mac.mm:38-41,56-72,93-119`) `[review]`. Only the Electron key's adoption and the wipe's delete stay Pane's.
  - `LocalState`'s rewrite of Chromium's file: `--profile-directory=Default`, and `active_time` and `tab_stats` cleared through local_state prefs in `PreProfileInit` and `PostMainMessageLoopRun` `[review]`.
  - Code written three times: one AES-GCM box on `EVP_AEAD`, as `whole_file.cc:72-125` does; every key derivation through `pane::encryption::DeriveKey`; `PanicWipe.cacheDirectory` as `chrome::GetUserCacheDirectory`; one stage-for-erase loop.
  - The 26 `pane.*` accessibility ids: `View::SetID` from one enum, as Chrome's `view_ids.h` does, which Views exposes as `AXDOMIdentifier` on the Mac and UIA's `AutomationId` on Windows (`CR/ui/views/accessibility/view_accessibility.cc:274-275`) `[read]`.
- **Pane's extensions page, details view, and shortcuts view** (`ExtensionsPage`, `ExtensionDetailsView`, `ExtensionShortcutsView`, about 1,000 Swift lines), and the engine code that copies Chrome's for them (`browser_context.mm:528`, `extension_shortcuts.mm`, `PaneBrowserContext.h:121-150`) `[review]`: Chrome's own pages, `chrome://extensions` and its shortcuts page, open in the profile's tab instead (D3).
- **The build around Swift:** XcodeGen and `project.yml`, SwiftPM, `framework.exports` and `chrome/BUILD.gn`'s 4-line order check beside it, `--pane-hosted`, and `app.yml` with `setup-app-build` (D1). The review plan's E9 and E15 already took `framework.order`'s check, `update-exports.sh`, and the framework's rename.
- **Patches that exist for an outside UI:** of the 12 files this plan first listed, the review plan returned 10 to the original. Left: `framework.exports`, and `extension_action_api.cc`, which an `ExtensionsContainer` replaces (X1). `chrome/BUILD.gn` stays edited for its `//pane` dep. ADR 0004 records, by patch, why the rest stays (E16, `ca6135f`). Two more go through seams `[read]`, each proved first:
  - `login_handler_views.cc`: `pane::ContentBrowserClient` overrides `CreateHttpAuthCoordinator()` (`CR/chrome/browser/chrome_content_browser_client.h:1302`) and its `CreateLoginDelegateFromLoginHandler`, which keeps the extensions' step (`http_auth_coordinator.cc:188`). `LoginTabHelper` still calls `LoginHandler::Create` itself for a main frame (`login_tab_helper.cc:214`), so `http-auth` and `proxy-auth` pass before the hunk goes.
  - `read_anything_side_panel_controller.cc`: a `SidePanelUI` of `//pane`'s built on `SidePanelUIBase`, which registers itself and leaves 3 overrides of 15 `[review]`. About 38 callers outside tests then get a panel that does nothing instead of null: read them first.
  - Not by a seam: `external_protocol_dialog.cc` and `extension_install_dialog_view.cc` have no view factory on 158, and a `_ForTesting` call in production fails presubmit. Only Chrome's stock dialogs would remove them, which U2 keeps as Pane's.
  - `browser_window_features.cc` stays: it makes Pane's `HostedWebContentsDelegate` (new tabs, the context menu, repost, a hung renderer, media), which design (ii) keeps.

## The user's decisions

Taken 2026-11-07, every recommendation accepted. Each is settled: carry it out, and ask again only if it hits a problem the user didn't weigh. U5 and U6 name what comes back to the user at Phase 5.

| | Decision | Why |
|---|---|---|
| **U0** | Views, one UI on both OSes, no duplicated code, Swift goes. | The user's request. |
| **U1** | Keep every profile in one window with hidden Browsers (ii), not Aside's in-place swap. | It keeps Pane's sidebar and the rule, and it already runs. It is the one place this plan differs from Aside's pattern. |
| **U2** | Stock Chromium, with Paper first where it shows: the page context menu (`RenderViewContextMenu`, native on the Mac; today's Electron order goes), file panels (same panels), the extension action popup (`ExtensionPopup`), and, since D3, the extensions page and its shortcuts page (`chrome://extensions`, in the profile's tab), which replace ADR 0062's. Pane's look stays for everything else an ADR chose on purpose: dialogs (0021), find bar (0067), extensions menu (0054), install dialog (0056), sad tab (0027), fullscreen hint (0025), quit (0053), progress bar (0081). Menus stay the system's, through `views::MenuRunner`. | −1,200 lines or so, and these stock pieces are what a browser's users expect. The rest is product. |
| **U3** | Freeze the Swift UI once Phase 1 passes: data-loss and security fixes only, none new, until cutover. | Otherwise every change is written twice, the thing the user asked to end. |
| **U4** | Release builds stay local, as engines are today: this Mac builds, `publish-engine.sh` uploads, and CI packages and attests. CI keeps lint, typecheck, knip, packaging, and attestation; scenarios run on this Mac before each push and tag (D1). Self-hosted or rented builders can come later. | "CI never builds Chromium" (ADR 0049) can't hold: `engine-key.sh:14` hashes `engine/pane`, so every UI commit has no prebuilt, and Chromium's internals are hidden symbols, so nothing links beside one. Honest cost: 284 tests leave CI and run before each push and tag; attestation vouches for packaging, not compiling. |
| **U5** | Windows gets its own promise, resting on the PIN: no encrypted volume, since one needs elevation, so `Extensions/`, file names, sizes, and times show at rest. ADR 0121's Engine keys cover the contents. Without a PIN, any same-user process can open the secret (DPAPI asks nothing). While unlocked, a same-user process can read memory. Taken at Phase 5. | ADR 0121's Mac guarantees rest on Keychain prompts and the hardened runtime, which have no plain counterpart. The README and Settings must say so. |
| **U6** | Windows needs a real x64 PC or a rented GPU machine for frame numbers, and an OV certificate ($150 to $300 a year) or the Store for public release. The alpha can go unsigned to testers. No Windows builder: the Mac's cross build is the release build (W1). Taken at Phase 5. | The VM has no GPU. Azure's signing service excludes individuals outside the US and Canada. |
| **U7** | The tree above, in one commit of its own, at Phase 0 (D2). | With `app/` gone, `engine/` no longer names anything apart. |
| **U8** | Windows after the Mac's cutover, compiling from Phase 1 on (G7), so nothing Mac-only creeps in. An alpha before Arc stops on 2026-12-22 is possible at this pace, not promised. | One cutover at a time. Encrypted data on a new OS needs its own audit. |

### Taken 2026-11-08, after the review

| | Decision | Why |
|---|---|---|
| **D1** | `main` becomes the Views line at Phase 0. A fix U3 allows ships from a `swift` branch cut from `v0.19.1`, whose engine key is the TSV's `46b8f8b35c24`; `release.yml`'s tag check accepts that branch, and AGENTS.md names it as the one exception to working and tagging on `main`. `app.yml` and `setup-app-build` go; CI keeps lint, typecheck, knip, packaging, and attestation, and scenarios run on this Mac before each push and tag. An engine-side Swift fix would need the branch's sources in a second checkout: ask then. | It removes the switch that would have kept both UIs on `main`: a gn arg, an engine-key formula in two modes, a `gn path` guard, an extra engine publish, `--ui views`, a second mode for `package.sh`, and every hook in `browser/ui` written twice behind a flag. |
| **D2** | The tree moves at Phase 0, in one commit, before any new file: `engine/pane` to `pane/`, the rest of `engine/` to `chromium/`, and `app/`'s remains to `packaging/` as the Swift goes. It replaces U7's "at the end". | Nothing is moved twice, and 39 ADRs aren't edited twice. The move keeps the engine key, which hashes git trees. |
| **D3** | Chrome's own extensions page and shortcuts page, `chrome://extensions` in the profile's tab, replace Pane's (ADR 0062), drawn in Paper first. The author had recommended keeping Pane's. | About 1,000 Swift lines and the engine code that copies Chrome's pages go, with every function kept: search, details, site access, Add a site, options, remove, on and off, pin, and shortcuts. |
| **D4** | An engine built with PGO (B6), measured against 0.19.1, and if faster, shipped with the `recover` fix as 0.19.2, once the user has seen the numbers. It becomes B1's baseline. | The gate then compares UI with UI on the faster engine. |

## What the review plan left (finished 2026-11-08)

`review-and-simplify.md` published 0.18.1, 0.18.2, 0.19.0, and 0.19.1, then removed itself (`89a6e31`):

- **0.19.1 is the fallback and the baseline:** `latest` on GitHub, on Chromium 158.0.8127.18 and the engine archive `46b8f8b35c24`, which `out/Release` holds. The real update passed against GitHub from 0.14.2 through 0.18.2 to 0.19.0, and from 0.17.2, 0.18.0, and 0.19.0 to 0.19.1.
- **Every upgrade path stays (ADR 0122),** reversing that plan's U1. C6 ports them.
- **The user kept E6, E7, E9, E12, E14's `@synthesize`, and E15,** so they're done.
- **Chromium 158 cost** 1.0 to 1.4% more memory and up to 3 ms more on a warm small page (`docs/benchmarks/README.md:16-33`), which the user accepted. On 158, `KeychainKeyProviderUserDataDirProtection` stays off, or cookies vanish at an update.
- **CI** runs its 13 original scenarios; `restore` and `fullscreen` fail now and then on a runner, and a rerun passes.
- **Still open:** Cut, Copy, Paste and VoiceOver by a person (`docs/human-checklist.md`); the profile's Group field is saved and shown nowhere (N1 asks first); `pane.roundedcorners.xyz` doesn't resolve.

## Phase 0: tools and baselines (about a day, plus B6's build)

**B0. The window's frame, first.** A test copy shares the user's preferences domain, since cfprefsd ignores `CFFIXED_USER_HOME` (`tests/lib/browser.ts:104-108`) `[read]`. So the user's saved `NSWindow Frame PaneMainWindow` sets the size of a render (1766 against 1898 px in one test), and resizing a test window moves the user's. Before B1 and B2, find how a test copy gets a fixed frame and writes none back, for instance a frame given at launch and no autosave under automation, and prove it. Read that one key of the user's, before and after, only with their OK.

**B1. Baselines,** on 0.19.1 or D4's 0.19.2, on a quiet Mac, into `evidence/views-migration/` (local), with instruments that work the same way on both builds:
- **The benchmarks,** from the published dmg: `bun tests/run.ts --binary <Pane.app>/Contents/MacOS/Pane --out <dir> --timeout 2400 tests/benchmarks/benchmark-{launch,unlock,pages,memory,stores}.ts tests/benchmarks/interaction-timing.ts`. Nothing of 0.19.1 is measured yet: the nearest is rc6, 0.19.0's code, against 0.18.0 (`evidence/review-2026-11-06/final-0.19.0/bench/`).
- **Launch:** `tests/tools/window-wait.swift` matches the first on-screen window at layer 0 `[read]`; it also requires `kCGWindowAlpha > 0`, since Views puts a window on screen at alpha 0 until its first frame (`CR/components/remote_cocoa/app_shim/native_widget_ns_window_bridge.mm:1022-1032`) `[review]`.
- **Input to frame:** `interaction-timing`'s signposts, which the Views build emits under the same names, from the input handled to the compositor's presentation (`ui::Compositor::RequestSuccessfulPresentationTimeForNextFrame`). Tab switches also stamp `SetTabSwitchStartTime`, so `Browser.Tabs.TotalSwitchDuration3` reads them over CDP. The recording stays as the felt check, at the display's own rate, not resampled to 60 fps: `evidence/review-2026-11-06/rig/felt2.sh` with `tests/rigs/frame-timing.ts`, which need the front.
- **Scroll,** which nothing measures today `[read]`: a scripted wheel scroll on a long fixture, counting Chromium's `PipelineReporter` frames, presented or dropped, in a `benchmark` trace over the benchmarks' CDP connection, in about 50 lines.
- **Memory,** by process type (`benchmark-memory`'s `byType`), with the browser process, where the UI lives, apart.
- **The tools, tracked:** one A/B runner (from `rig/bench2.sh`, given binaries, not a folder in `/tmp`) and one image diff (`pngdiff.swift`) move from the ignored `evidence/` into `tests/`.

**B2. Renders.** Every surface, in every state, in light and dark, at 2×, from the Swift UI, by name. None are kept: the review plan's 136 and its 74 pairs went with `/tmp`, and `evidence/review-2026-11-06/final-0.19.0/qa/` holds captures at 1200 px, not named renders at 2×. Make them, after B0, from the 11 scenarios that call `app.render` (`data-volume`, `extension-details`, `extension-shortcuts`, `extensions-menu`, `http-auth`, `profile-lifecycle`, `profile-sheet`, `shortcuts`, `quit`, `security`, `updates`), in light and dark, and add what they miss. The Views build renders through `ui::Layer::RequestCopyOfOutput`, leaving the page out as the Swift render does (`Automation+Commands.swift:806-821`). These and Paper are what Phase 3 compares against.

**B3. The edit loop, on `out/Release`.** No component build: the release config isn't official, so it has no ThinLTO, and its last incremental build ran 342 steps in 24 s, the framework's link 8.2 s of it (`out/Release/.ninja_log`) `[measured]`. Run `out/Release`'s `Pane.app` as built, skipping `package.sh`. Measure one UI edit to a running window against the gate's 30 s, and add a component build only if it misses.

**B4. The Windows toolchain.** The VM's half is done (the log, 2026-11-07), and 158 asks for the same `TOOLCHAIN_HASH`, `3bfcb536c8`, and SDK, 10.0.28000.0 (`CR/build/vs_toolchain.py:62-63`), so the zip holds. `win` goes into `sync.sh`'s `target_os`, since `sync.sh` writes `.gclient` with `["mac"]` and syncs with `-D` on every run (`sync.sh:34,57`) `[read]`, with `DEPOT_TOOLS_WIN_TOOLCHAIN_BASE_URL` at the zip's folder and `GYP_MSVS_HASH_3bfcb536c8=dcda99cac7`. The zip stays outside the repo: Microsoft's files aren't Pane's to publish.

**B5. Where Swift ships from** (D1): the `swift` branch from `v0.19.1`, `release.yml`'s tag check taught to accept it, AGENTS.md's exception for it, and `app.yml` and `setup-app-build` removed, their engine steps moved into `release.yml`.

**B6. An optimized engine** (D4): `chrome_pgo_phase = 2` in `release.gn`, and `checkout_pgo_profiles` in `sync.sh`'s `custom_vars`, which fetches the profile `CR/chrome/build/mac-arm.pgo.txt` names for this branch `[read]`. PGO needs no official build here: `CR/build/config/compiler/pgo/BUILD.gn` asserts only a profile path `[read]`. One full rebuild, about 3.5 hours at 6 jobs, then B1 against 0.19.1; Chrome's own releases are built this way, and a gain of about 10% on page and script work is an estimate. ThinLTO (`is_official_build`) only once its link is timed, since it links with every core (`--thinlto-jobs=all`) and ignores `build.sh`'s cap. If it measures faster, show the user the numbers, then publish it with the `recover` fix as 0.19.2 from the `swift` branch, and take B1 on it. The same flags carry to Windows (W1).

**B7. The tree** (D2): `engine/pane` to `pane/`, the rest of `engine/` to `chromium/`, in one commit, then fix what names them, by grep until none does (72 files and 192 lines today), and `sync.sh`'s link.

## Phase 1: the prototype, and a gate (about 3 days)

The riskiest parts first, on the real engine, as little as proves them. **Nothing else starts until this passes.**

**G1. The window, and the bundle.** The Widget `WindowHost` already makes, with a root view of Pane's, and the frame: the lights at (12, 12), drag by `HTCAPTION`, full screen, through `BrowserNativeWidgetWindow`'s title-bar seams. ADRs 0025, 0036. The bundle runs Chromium's loader and helpers: `package.sh` stops overwriting the executable, with plist overlays (`LSMinimumSystemVersion` falls to 13.0 unless overlaid), licenses, CRLSet, strip, signing and entitlements (Chromium's list adds bluetooth, usb, photos, print: keep Pane's), and icons through `actool`.

**G2. Two profiles, one window.** A bare sidebar listing both profiles' tabs, switching instantly, over the hidden Browsers.

**G3. The page.** One `views::WebView` per window, in place of the `NativeViewHost` E7 put the app's view in. Clicks, VoiceOver, live resize, hit-test time, as E7 checked them, and the window's single compositor.

**G4. Over the page.** A scrim and a sheet over a live page: hit testing, focus, keys into the sheet and not the page, animation over a playing video. `NativeViewHostMac` leaves clips and hit-test insets `NOTIMPLEMENTED` (`native_view_host_mac.mm:254-271`), and Chrome's own find bar is a child Widget with key-window bugs, so this is the first thing to prove with plain views.

**G5. The look.** One spring, through Phase 3's motion driver, and the page's corners as layer radii.

**G6. Keys,** with no router: ⌘T and ⌘L ahead of the page, a key queued behind them, ⌘1-9, Esc and Return in an overlay, through the `FocusManager`'s accelerators and `PreHandleKeyboardEvent`. ADRs 0019, 0079; `key-precedence` and `shortcuts`.

**G7. Windows compiles.** `gn check` on a Windows config, and `//pane`'s objects compiled for x64 (`autoninja -C out/Win ../../pane/<file>.cc^`), with no full Windows build, before each push from here on. Start `assert(is_mac)`'s removal (`engine/pane/BUILD.gn:7`). The first Windows run is Phase 5: Chromium already draws a scrim and overlay WebViews over a page in one Widget on every OS (`frame/contents_container_view.cc:127,279-297`) `[review]`.

**G8. The channel's core.** The same JSON as today, made event-driven: `base::FilePathWatcher` (kqueue on the Mac) in the app and `fs.watch` in the runner, in place of a 100 ms poll and a 50 ms sleep (`Automation.swift:50`, `tests/lib/pane.ts:299`), which costs up to about 150 ms per command. Then `state`, real input posted through the platform's event path (ADR 0012), `render`, and the commands the benchmarks and these scenarios call (`createProfile`, `openPage`, `select`, `menu`), gated as ADR 0115 says. Each surface ports its own commands after.

**The gate,** against B1, on the same Mac and day:

| Measure | Budget |
|---|---|
| Tab switch, input to frame | ≤ baseline |
| ⌘T and ⌘L, key to frame | ≤ baseline |
| A painted frame reaching the screen | ≤ +1 ms |
| Scroll, dropped frames | ≤ baseline |
| One UI edit to a running window | ≤ 30 s |

Launch, memory, page load, and unlock wait for the real UI, since a prototype with no vault, no start order, and no extensions can't fail them for a UI reason. They run with the same command at the end of the shell, at the end of the Lock, and at the cutover, and hold what 0.19.0 held:

| Measure | Budget |
|---|---|
| Launch to the first painted frame | ≤ +5 ms |
| PIN to the first page, and launch without a PIN to the first page | ≤ +25 ms |
| Memory at 1 and 10 tabs | browser process ≤ baseline, total ≤ +1% |
| Warm page load | ≤ +3% |

Show the user the numbers and a recording. **If a budget fails and has no fix in sight, stop and tell the user:** the fallback is to keep Swift on the Mac, which this plan was written to avoid, so it's their call. Passing starts U3's freeze.

## Phase 2: the model and the data (about two days, 4 scenarios)

Port PaneKit (5,977 lines, 284 tests) into `pane/model`, test by test, the case names kept, with ADR 0122's paths in `pane/model/mac`. The formats come first, since a mistake here locks a user out or erases them.

**C1. The proof, before code.** "Byte for byte" means keys, types, nulls, envelopes, AAD, and KDF inputs. Salts and nonces are random (`DataKey.swift:48-51`), and `base::JSONWriter` spells some characters and numbers otherwise than Swift's encoder does (`/`, `<` as `<`, 260 as `260.0`: `CR/base/json/string_escape.cc:41-78`, `json_writer.cc:92-98`), which the readers on both sides accept `[review]`. So:
- **Vectors:** fixed ones for every KDF, and one box per format sealed by the published Swift. The engine key's and `noiseSeed`'s exist (`DataKeyTests:22`, `ClientHintsTests:238`).
- **The swap pass:** `files-migration` already runs each earlier build, then this one, then the earlier one again (`files-migration.ts:31-46,692-697`) `[review]`. Add 0.19.1 to it, both ways: 0.19.1 reads back what the Views build wrote, cookies included. That is both directions, with no stored corpus.
- **The oldest rows,** the Electron app and 0.13.1 and before, which the harness can't drive (`docs/qa/review-and-simplify/report.md:60`) `[read]`: the synthetic fixtures of `migration`, `identifier-move`, `security` (the bcrypt PIN), and `proxy`.
- **Two traps.** `SecurityState.recover` missed every hash 0.13.1 wrote, since its JSONEncoder escaped each `/` as `\/` (fixed in the Swift on 2026-11-08: the log). And the sealing key's input is the Keychain item's stored bytes, its 24-character base64 text, not 16 decoded bytes (`SafeStorage.swift:113`) `[read]`.

**C2. The formats, exactly as they are:**

| Thing | Keep |
|---|---|
| `security.json`, copy `Data.security.json` | `{"state":{"failedAttempts","key":{"box","pin":{"rounds","salt"},"secret"},"pin":{"hash","length"}},"version":0}`, sorted keys; the copy read only when the first has no key. `pin.hash` is the bcrypt of the Electron app and of 0.13.1 and before (`Security.swift:12`), checked once at the first unlock, when the PIN seals the key and the hash goes. `SecurityState.recover` (`Security.swift:66`) rebuilds a damaged file from `hash`, `length`, and `failedAttempts`, and the wipe relies on it (`PanicWipeRun:70`) |
| Data key | 32 random bytes; `box` is AES-256-GCM, no AAD, base64 of nonce(12) ‖ ciphertext ‖ tag(16) |
| Sealing key | HKDF-SHA256(the Keychain item's stored bytes, salt = the PIN's output or empty, info `Pane data key`, 32) |
| PIN | PBKDF2-HMAC-SHA256(UTF-8 digits, 16-byte salt per seal, the file's rounds, 600,000 written, 32); BoringSSL's `PKCS5_PBKDF2_HMAC`, since `crypto/kdf.h`'s is SHA-1 |
| `secret` | First 16 bytes of HMAC-SHA256(key secret, `Pane data key secret`), base64 |
| Derived keys | HKDF, 32: `Pane files`, empty salt; `Pane engine files`, salt the profile's id; the volume's passphrase, base64 of `Pane data volume, files encrypted`, through stdin |
| `profiles.json` | `{"box"}`, AES-256-GCM under the files key, AAD `profiles`; plaintext `{"profiles":[…]}`. Required: a profile's `id`, `name`, `tabs`, `createdAt`, and `updatedAt`; a tab's `id` and `url`; and a fingerprint's, when it has one, `userAgent`, `platform`, `screen.width` and `height`, `language`, `languages`, `hardwareConcurrency`, `deviceMemory`, and `maxTouchPoints`. A missing one fails the file: empty sidebar, `.damaged` kept before the first save. Everything else has a default (ADR 0122, `Profile.swift:70-72,148-159,221-224,291-302`): `color` blue; `group`, `fingerprint`, and `proxy` absent; `keepLoaded` false; `savesHistory` true; `historyClearOwed` its opposite; a tab's `title` and `favicon` empty; `timeZone` empty; `webgl`, `canvas`, and `audio` noised; `colorDepth` 24 |
| `settings.json` | Envelope `settings`; missing means default, null means off, so writers emit null |
| `deletions.json`, `migration.json`, `Pane.lock` | As they are; `Pane.lock` shared with the watchdog, by `flock` |
| Volume | `Data.sparsebundle`: UDSB, 1T, no file system, APFS container, one encrypted volume `Pane`, mounted at `Engine/Volume`, refused unless encrypted. Links `Engine/<id>` to `Volume/<id>/Profile`, caches to `Volume/<id>/Cache`, `.files-encrypted` |
| Wipe | `Wiping/<uuid>/` resumes at launch; due only with a PIN, failed ≥ 5, no key; failed ≥ 5 with a key resets |
| Keychain | Service `Pane Safe Storage`, account `Pane`, 16 random bytes as base64 text |
| Window frame | `NSWindow Frame PaneMainWindow`: read once, so the window comes back where it was |

Bcrypt stays (ADR 0122): its C (`app/PaneKit/Sources/CBcrypt`, OpenBSD's, credited in `third_party/openbsd-bcrypt`) builds in gn as it is, on the Mac only, and only its check is called: `PinHash.make` has no caller but its test `[read]`. Line references: `DataKey:50,81-121`, `SealedFile:34`, `Profile:59-265`, `ModelStore:49-54,85-88`, `AppSettings:91-97,112-115`, `AppLock:69,77`, and `SafeStorage:9-39` at `989d176`; `DataVolume:121,154,289-293,310` (the image made, `whenReady`, the refusal without encryption, the mount) and `PanicWipeRun:69-75` (`isWipeDue`) at `8bee4e5`.

**C3. The rules, plain C++.** `BrowserModel` (`ObserverList`, the 300 ms save through `ImportantFileWriter` with a `BackgroundDataSerializer`, so the JSON, AES-GCM, and base64 pass never runs on the UI thread, where Views composites: `CR/base/files/important_file_writer.h:152` `[review]`), `Profile`, `AppSettings`, `ProfileDraft`, `ProfileForm`, `ProxyRoute`, `EngineFingerprint` (`noiseSeed` exact), `SleepScheduler` (`base::Clock`), `TimeZones` (ICU), `TabHistory`, `TabSwitcher`, `Reorder`, `CrashLoop`, `PinEntry`, `ExtensionPromptText`, `ExtensionSite`, `AddressInput` (Pane defaults to https, `FixupURL` to http), `ToolbarProgress`, `Updates`, `UpdateInstall`. ADR 0077's split stays, in C++.

**C4. The vault, on task sequences** (`pane/model/mac`). One `SequencedTaskRunner` for the volume (attach, unlock, move in, purge, detach), and one per profile, running `BringProfileUpToDate()`: the plain folder, the restore, the conversion, the repair, the markers. `InvalidateWeakPtrs()` at each Lock, and `WaitableEvent::TimedWait(5 s)` in `ProcessExiting`. The order tasks are posted in gives ADR 0116's and 0121's rules: detach after attach and move, the PIN after detach, the window's profile first, a restore and a migration never together, and no forced detach while the engine runs (SIGBUS; `DataVault:598-599`; the one forced detach is at quit, with the engine stopped, `:567-576`). That replaces `DataVault`'s counter, five tasks, waiter maps, and 5 s run-loop spin, about 400 lines `[review]`. Its units: `DataVault`, `AppLock`, `DataLock`, `DataVolume`, `PanicWipe` and `PanicWipeRun` (the wipe at launch, with the Electron app's branch), `ProfileDeletions`, the Keychain through Chromium's code, and `pane-volume-watch` as a gn executable, which keeps what the watchdog does: `setsid` with SIGHUP ignored, `Pane.lock`'s `flock` once the app is gone (not `base::File::Lock`, which is `fcntl` and wouldn't keep a published Pane out), the image found by its path, and a clean detach at the end (`app/VolumeWatch/main.swift:18-19,80`) `[read]`. Keep 0.19.1's fixes: `whenReady` waits up to 10 s for a disk just made, while a wrong passphrase or a locked volume fails at once; the quit's and the Lock's fallback detach a remembered device only if `holds(device:)` still finds Pane's image there; the watchdog ends at quit; the volume's tests run one at a time.

**C5. Start, in `PaneMainDelegate::BasicStartupComplete`.** With Chromium's loader, no Pane code runs before `ChromeMain`, and `PreBrowserMain` is too late: `PreSandboxStartup`'s `InitializeUserDataDir` makes `<root>/Engine` and starts Crashpad, after which `moveFormerData` finds the root taken, and a 0.13.1 user would start empty (`CR/content/app/content_main_runner_impl.cc:962,1060,1279`) `[read]`. `BasicStartupComplete` runs first, can return an exit code (the Electron alert's Quit), and filters argv; `engineDidStop` goes in `ProcessExiting`. The order, from `main.swift:9-101`: the data folder; `moveFormerData`, only without `PANE_DATA_DIR`; `removeSessionCaches`; `removeFormerUserCaches`; the folders at 0700; the defaults; `stageEngineTraces`; `PanicWipeRun.atLaunch`; `LaunchMigration.run`; `dropPendingExtensions`; `DataLock`; `attachImage`, on a `base::PlatformThread`, since no thread pool exists yet; then the engine. A due wipe erases before the Electron app's data migrates, and the former identifier's folders move before anything reads them. With the argv filter and the feature list (`browser_application.mm:265-323`), or `--remote-debugging-port` reaches an open profile (ADR 0115:17); the list keeps `KeychainKeyProviderUserDataDirProtection` off, or cookies vanish at an update (158). Read less on the way: look for the Electron app's data before reading `profiles.json`, and read `migration.json` once, not three times (`ElectronMigration.swift:89-126`, `PanicWipe.swift:41-45`) `[review]`. Find the Keychain secret at launch, find-only, so an unlock doesn't wait on the Keychain after the last digit (`AppLock.swift:229-237`).

**C6. Every upgrade path (ADR 0122),** in `pane/model/mac`, read exactly as the Swift reads it, by the same tests: `LaunchMigration` and `LaunchAlert` (129 and 104 lines) with `ElectronMigration` (257) for the Electron app's data (ADR 0043); `BundleIdentifier`'s move of the former identifier's folders (126, ADR 0114); plain profile folders into the volume, and a plain `profiles.json` sealed with its proxy passwords kept apart (`ProxyPasswordStore`, `ProxyPasswordVault`, `ProxyPasswords`, 256, their Keychain calls through Chromium's); a bcrypt PIN, checked once; the volume's former passphrase (`DataVolume`); the files' conversion behind the window, through `//pane/encryption/profile_migration`; and 0.18.0's repair from its safety copy (ADR 0121). The smaller steps too: `PanicWipe.stageEngineTraces`, `BundleIdentifier.removeSessionCaches` and `removeFormerUserCaches`, `ElectronMigration.dropPendingExtensions`, the wipe's branch for the Electron app's data, and the former mount names `UpdateInstall` knows. And their timing: the conversion runs before a profile's context opens (`ProfileContexts.with`, then `vault.prepare`, then `ensureFilesEncrypted`); `profileDidOpen`'s 5 s timer counts the opens; `migrationsFinished` holds a Lock's detach until they end; 07.5 shows only past a second. The proof is this phase's four scenarios, `migration`, `identifier-move`, `files-migration` with `PANE_EARLIER_BUILDS` and `PANE_REPAIR_FROM`, and `files-migration-large`, with `security` and `proxy` from Phase 3.

## Phase 3: the surfaces, one loop (about 8 days, 77 scenarios)

Each surface, in one commit or a few: the design primitives it needs, its channel commands, its scenarios passing on Views, its renders matching, a one-line note at the top of each ADR it changes (AGENTS.md's form, with a full rewrite only where the rule itself changes), and its Swift and bridge deleted. The meter: the scenarios against `main`'s build print N of 81, by area, and the log records it at each surface's end; 55 of the first 77 should need no edit once their commands exist `[review]`. AppKit strings in 12 scenarios and direct AX in 10 more become view ids or channel reads, each keeping a real input: a scenario that stops pressing what it tests has lost its point. Of `tests/tools`' 9 Swift tools, port what the channel can answer, and keep a macOS probe only if nothing else can do it. The full budgets run after the shell and after the Lock.

### The design primitives (`pane/ui/design`), one source each, added as a surface first needs them

- **Tokens:** `Palette`'s light and dark pairs as one `ui::ColorMixer` added at startup, and views take `ui::ColorVariant`, so a Theme change re-resolves everywhere and native menus follow (`ui/color/color_provider_manager.h:47`) `[review]`. `NativeTheme` and `ThemeService::SetBrowserColorScheme` in place of `NSApp.appearance` (ADR 0001). Scrims black in both.
- **Squircle:** radii 4, 7, 9, 11, 15, 18, 22, 29 on |x|⁴+|y|⁴=1, capsules round, rings by `circularEquivalent`. Each corner from Blink's two cubics (`path_builder.cc:35-98`), cached by size, in place of `Squircle.swift`'s 4r segments (464 points at 29), kept only if it matches today's within a fifth of a pixel. Layers stay circular under the 0.57 rule (ADR 0057).
- **Motion:** one table of the 16 curves (cubic or spring, duration, delay), evaluated with `gfx::CubicBezier` or the spring's closed form, and one driver stepping on compositor frames (`AnimationDelegateViews`, not a 60 Hz timer), animating layer transform and opacity, never repainting; the progress bar's display link moves onto it. `views::AnimationBuilder` takes only `gfx::Tween` types, which match none of Pane's curves but the linear one `[review]`. Reduced motion from `gfx::Animation::PrefersReducedMotion()`. Hover 0.15 s ease-out, selection instant (memory `craft-animate-details`). If the UI thread drops frames, a compositor-thread path (`AddThreadedAnimation` with a sampled curve) is the option to measure.
- **Text:** weights, `Label::SetLineHeight` for the line boxes, subpixel rendering off (ADRs 0034, 0060). Tracking and tabular digits have no Views path without a `ui/gfx` patch (`font_render_params.h:54-74`, `render_text_harfbuzz.cc:1471`) `[review]`: their 9 uses are drawn without them, in Paper first, for the user.
- **Icons:** Lucide's SVG strings as they are, parsed with `SkParsePath`, stroked with round caps and joins at the width asked for, and cached. Chromium's `.icon` format has no joins or dashes (`vector_icon_types.h:23-71`). If an upgrade drops `SkParsePath` from the build, a small parser.
- **Controls:** stock bases with one Pane painter. `views::Textfield`, which brings IME, undo, Cut/Copy/Paste, Services, and accessibility, with `FocusRing` on a squircle path; `Separator`; a `ResizeArea` subclass for the sidebar's edge; one squircle background and border with hover 4%, press 6%, and disabled 40%; Pane's own switch (36×20, where `ToggleButton` is 26×16) and the system checkbox (ADR 0058); no ink-drop ripple; a field's problem as a red ring (memory `field-problem-red-ring`); the scroll fade painted as two overlays in the background color at alpha 1−share², not masked, since `LinearGradient` holds 8 steps and a mask forces a render surface, under memory `fade-only-without-limiter`'s rule.
- **Menus:** one `ui::SimpleMenuModel` per menu, with `ui::Accelerator`s as the one command table, shown by `views::MenuRunner`, an NSMenu on the Mac (`menu_runner_impl_mac.mm:20-24`) `[review]`, anchored at the trigger's corner; the menu bar from the same models. The Mac's own menus, as ADRs 0058 and 0070 require.
- **No gallery:** each control is checked in its surface's renders and against Paper's state boards.

### The shell (about 2 days, 25 scenarios)

- **H1. Window:** `RootView`, `MainWindowController`, `EmptyStateView`, `PageHostView`. New windows, close sequence (ADRs 0016, 0020).
- **H2. Sidebar:** `SidebarView`: profiles, tabs, the proxy dot, row menus, the delete alert; `FadingScrollView`, `SidebarResizeHandle`, `Favicon`. Sleeping tabs have no `WebContents`, so the model stays Pane's. Favicons are drawn from `ContentFaviconDriver`'s image as it comes, and encoded only to be saved, off the UI thread: today each update encodes a PNG on the main thread and the row decodes it again (`BrowserController.swift:801-806`) `[review]`.
- **H3. Reorder:** `VerticalReorder`, its live slide and `geometryGroup` rule (ADR 0066), with `CR/chrome/browser/ui/views/tabs/vertical/` as the reference.
- **H4. Toolbar:** `ToolbarView`, the address field and its label over the field, `selectAddressIfOwed`'s behavior with none of its code, `ToolbarProgressBar` (ADR 0081), `UpdatePill`.
- **H5. Tabs:** `BrowserController`, minus the bridge: `interactionAllowed`'s one gate, Sleep, Unload, Trim, Keep loaded, restore, popups as tabs (ADR 0015), the tab switcher, the crash loop, and `AfterFrame`'s order.
- **H6. Panels:** `SettingsView`, the page removed beneath (ADR 0073), `AppIconPicker`, the Dock icon.
- **H7. Keys and menus:** every shortcut from the one command table, ⌘Q's dialog, Ctrl-release's commit.

### What Chrome asks for (about 2 days, 17 scenarios)

Hooks now call the UI directly: no request objects, no Objective-C, no adapters.

| Surface | Today | In Views |
|---|---|---|
| JS, sign-in, external app, hung page | `DialogView`, `DialogPresenter`, `dialogs.mm` | One `ui/dialogs` component, the macOS-alert look, 260 wide (ADR 0021); sign-in through the `CreateHttpAuthCoordinator` seam |
| Permissions, device chooser | The same | The same component, centered Allow / Allow This Time / Don't Allow. The hook stays: the one view factory, `PermissionRequestManager::set_view_factory_for_testing` (`CR/components/permissions/permission_request_manager.h:280`), is for tests. Default BLOCK for chooser settings (E5) |
| Find | `FindBar` | Pane's bar, 357×54 (ADR 0067), on `FindBarController`, as a plain view, not Chrome's child Widget |
| Context menu | `PageContextMenu`, `extension_menu` | Stock (U2), Paper first |
| File panels | `select_file_dialog` | Stock (U2) |
| Print | Pane's panel call | Kept (ADR 0021) |
| DevTools | `devtools_window_host` | A second `WebView` beside the page, placed by the `DevtoolsUIController` subclass E6 made (ADR 0024) |
| Sad tab | `SadTabView` | A plain view; the first crash reloads (ADR 0027) |
| Full screen | `ExclusiveAccessHint` | The 3.5 s pill (ADR 0025) |
| Tab-modal host | `modal_dialog_host` | `WebContentsModalDialogHost` on the content area |

### Extensions (about a day, 13 scenarios)

- **X1. Toolbar and menu:** `ExtensionActionsView`, `ExtensionsMenu`, Aside's menu (ADR 0054), per profile (ADR 0031), through `MenuRunner`. Register an `ExtensionsContainer`, as `webui_browser_window.cc:190-194` does: that replaces patch 0005's `extension_action_api.cc` hook.
- **X2. Popup:** stock `ExtensionPopup` (U2) for `action_popup` and `ExtensionPopover`. Paper first: `NSPopover`'s corner changes (ADR 0057).
- **X3. Install and prompts:** `ExtensionPromptView` in `ui/dialogs` (ADR 0056), `ExtensionPromptText`, `web_store`.
- **X4. The page (D3):** `chrome://extensions` in the profile's tab, where Pane's page and details view were: Paper first, showing it inside Pane's window. Prove the WebUI runs over a hidden Browser `[inference]`, point the extensions menu's manage item at it, and have the scenarios that drove Pane's page (`extension-details` and its siblings) read Chrome's through CDP. ADR 0062 is rewritten for it.
- **X5. Shortcuts (D3):** `chrome://extensions/shortcuts`, the same way. `pane::ExtensionShortcuts` stays, for the one-FocusManager reason above (ADR 0098).

### Profiles and network (about a day, 11 scenarios)

- **N1. The sheet:** `ProfileSheet`: identity, history, fingerprint, zone, proxy test, the 429-zone menu, swatches. Over the page, as G4 proved. `group` is edited and shown nowhere: ask before porting it.
- **N2. Proxy:** `ProfileNetwork`, `SocksRelay` on `net::TCPServerSocket` (net's SOCKS5 has no auth), `ProxyTest` on `SimpleURLLoader`. ADR 0037.
- **N3. Contexts:** `ProfileContexts` on `browser_context`, which becomes plain C++.
- **N4. Fingerprint:** the host-specific defaults (`ProfileDraft:8`, `EngineFingerprint:75,83`) become the platform's, without changing what a Mac profile has saved.

### The Lock and security (about a day, 8 scenarios)

- **L1. Screen:** `LockScreenView`, against Paper 07.1 to 07.9, keys in its dots, every wrong path. States 07.6 to 07.8.
- **L2. Triggers:** `ui::AddScreenLockCallback` (`CR/ui/base/idle/idle.h:41`), `CalculateIdleTime`, `PowerMonitor`, and one small platform pair for display sleep and session resign, which nothing in Chromium observes; `ui::SessionChangeObserver` exists only on Windows `[read]`. ADRs 0042, 0116; the detach order.
- **L3. Audit:** `files-encrypted` strict, `engine-keys`, `private-traces`, `volume-unindexed`, the disk check with a control, and a same-host comparison with the published dmg (memory `check-claims-by-grepping-disk`). ADR 0115's hardened runtime still holds on the new bundle: `lldb` refused.

## Phase 4: the cutover (about a day)

- **K1. The update path, both ends.** Port `Updater`, `DmgDownload` (`SimpleURLLoader` has no resume data: range requests), and `UpdateInstaller`. Published Panes must still stage it: `Pane.app`, the identifier, `PaneVersion`, `-arm64.dmg` (`UpdateInstaller:55-60`). Before publishing, `update-install`'s stand-in server with the published 0.19.1 and the real Views dmg (it needs a dmg option); the live update can only follow K4, since Panes read only `releases/latest`.
- **K2. The candidate is "Done when", on the Mac:** 81 of 81, the budgets against the fallback, `pane_unittests` plain and sanitized, the checklist below, every Paper board compared. Sanitized means one ASan out dir with `dcheck_always_on`, on a test target of `pane/model` alone (//base, //crypto), not on `pane_unittests`' network stack. TSan doesn't build on the Mac (`CR/build/config/sanitizers/BUILD.gn:567`) `[read]`. Results in `docs/benchmarks/views-vs-swift.md`, as `native-vs-electron.md` was.
- **K3. What's left to delete, and the records.** `--pane-hosted`, `public/`, `app/project.yml`, `framework.exports` with `chrome/BUILD.gn`'s check, `knip.json`'s `swift`. `release.yml`: the tag checks stay, PaneKit and XcodeGen go, and it packages the archive `publish-engine.sh` uploaded (U4). AGENTS.md's gates: lint, typecheck, knip, `clang-format` over `pane/`, `gn check`, the OS-flag grep, `pane_unittests`, the scenarios touched. ADR 0004 rewritten as its subject's whole decision, its count updated; one new ADR for U0 to U8 and D1 to D4; 0009, 0010, 0049, and 0051 rewritten for what changed; the rest already carry their notes from Phase 3. README, AGENTS.md, DESIGN.md, CONTEXT.md, `docs/building.md`, and `engine/UPGRADING.md` describe what exists.
- **K4. Publish,** with the user's OK, then the live update from published builds against GitHub, with ADR 0122's rollback (`gh release edit` puts the latest back). The user keeps the last Swift dmg's link until a week has passed.

## Phase 5: Windows (1 to 2 weeks, with waits on the user)

The UI, rules, and channel already compile here (G7). What's left is what only Windows has.

**W1. Build.** One x64 out dir, run under emulation in the arm64 VM for behavior (memory `windows-test-vm`), frame numbers on a real PC (U6). The Mac's cross build is the release build: Pane runs no crash handler (patch 0008), so a cross build's missing Crashpad reporting costs nothing, but 0008 needs its Windows twin, since `chrome_elf` starts Crashpad there (`chrome_elf_main.cc:26`, `crashpad.cc:252-257`) `[review]`. B6's flags, with Windows' PGO profile (`CR/DEPS:4724-4739`) `[read]`. The engine-key TSV's platform column. `chromium-watch.yml` gains Windows' channel.

**W2. Encryption, the dangerous part.**
- Register each root twice, as given and as `base::NormalizeFilePath` returns it (long names, junctions followed, the case on disk), as the Mac registers links and resolved paths, so `KeysFor` stays string compares with no disk access on each open (`engine_keys.h:44-49`, `CR/base/files/file_util_win.cc:1002-1040`) `[review]`. Still refuse a path form nothing registered rather than pass it through as plain (`engine_keys.cc:219`).
- Narrow-path code won't compile (`sqlite_vfs.cc:213-214,754,930,966,997`; `engine_keys.cc:79,98`). The hook in `file_util_posix.cc` needs its twin at `file_util_win.cc:1319`.
- **The HTTP cache:** Windows doesn't default to Simple (`IsSimpleBackendEnabledByDefaultPlatform`), and the backend experiment's own default is SQL, which Pane's hook doesn't cover: force `DiskCacheBackendExperiment:backend/simple`, spelled out (`CR/net/base/features.cc:564-571`) `[read]`, then measure it on NTFS.
- No volume means no links, no junctions, and no conversion: no Windows Pane was ever published, so every profile starts encrypted.
- `quarantine_win.cc:138-151,206` writes URLs.
- `files-encrypted` and the disk check are the proof, with a control.

**W3. Data (U5).** `%LOCALAPPDATA%\Pane`, os_crypt's DPAPI, no volume. `Extensions/` encrypted too if the user wants it (ADR 0121 put it at a week). The not-indexed attribute in place of Spotlight's markers. The wipe waits for the engine's processes with `base::ProcessIterator` and `WaitForExitWithTimeout`, as on the Mac, with no job object.

**W4. Platform files,** as the table says: caption buttons, painted by Chrome's Windows 10 and 11 glyph painter (`frame/windows_icon_painter.cc`); Snap Layouts from `HTMAXBUTTON` (`CR/ui/views/win/hwnd_message_handler.cc:3407-3420`); nothing for jump lists, which only `BrowserView` makes; Pane's identity in Chromium's install modes (`CR/chrome/install_static/chromium_install_modes.h:24,58-59,69`), one patched file, so Pane never collides with an installed Chromium, and `ui::win::SetAppIdForWindow` on Pane's window; `Alt+F4` (ADR 0053); Ctrl from the one command table; Chrome's Windows rules for extension shortcuts (ADR 0098).

**W5. Paper first (ADR 0058).** Caption buttons in place of the lights. A home for the menu bar's commands. Ctrl on keyed chips and the shortcuts dialog. Copy naming Mac, macOS, or Dock. Segoe at 12 and 13. Backdrop, solid unless the user asks. Installer, taskbar icon, scrollbars, high contrast. Wait for the user's approval.

**W6. Scenarios.** The runner's Mac tools (`processes.ts`, `tools.ts`, `cua.ts`'s `ioreg`, `browser.ts`'s `CFFIXED_USER_HOME`, `pane.ts`'s `sample`) behind one module each. A primary modifier in place of `"command"` (9 scenarios). ADR 0122's four (`migration`, `identifier-move`, `files-migration`, `files-migration-large`) stay Mac-only, since every row of its table is Mac data. `applescript`, `volume-unindexed`, `data-volume`, `memory-guarded`, `update-install`, and `private-traces` need redesign or dropping. In the VM for behavior; on a real PC for frames (U6).

**W7. Install and update,** one of two, decided here:
- **Chromium's own:** `mini_installer` as `Pane-<version>-x64.exe`, which Pane's update check downloads, checks by SHA-256, and runs silently. `chrome.exe` loads `<version>\chrome.dll` and swaps in a waiting `new_chrome.exe` at quit, and the shortcuts, the uninstall entry, and default-browser registration come with it (`CR/chrome/installer/setup/install_worker.cc:325-347,864-904`) `[review]`. Its costs: each release needs its own 4-part Chromium version, which is a patched `chrome/VERSION`; Client Hints' `fullVersion` becomes fixed; the installer's product name needs a patch.
- **Pane's own:** a small launcher over `app-<version>` folders, at one more process per launch.

ADRs 0002, 0044, 0046, 0092.

**W8. Audits nothing else catches.** ADR 0040's quiet check again (Hunspell downloads, `spellcheck_hunspell_dictionary.cc:280`). `GetBrowserViewForBrowser`'s call sites on Windows-only paths. ADR 0071 with UIA and Narrator. Default browser (ADR 0109).

**W9. Alpha,** with the user's OK (U6, U8): `Pane-<version>-x64.exe`, the Mac's update check untouched, its asset name kept apart. Then the README says what's promised on Windows (U5).

## What not to do

- **No second UI, ever.** Nothing is written for one OS that could be written for both. A Windows bug fixed by a branch in `ui/` is fixed wrong.
- **No WinUI, no WinRT, no C API, no bridge.** The measures that moved this decision are exactly what those cost.
- **No edits in `chrome/browser/ui/views/`** while a seam or `//pane` can do it. `BrowserView` stays unedited.
- **No Brave-style `chromium_src` overrides.** They are edits in disguise, a stale `#define` fails silently at an upgrade, and they'd hide from ADR 0004's count.
- **No redesign in passing, and no new features during the migration.** A port matches its board, and anything shown differently goes through Paper and waits for the user. Omnibox suggestions, a downloads UI, and site settings stay out, however close Chromium's are.
- **No data format change,** not even a tidy one: C1 is the fallback.
- **No history rewrite.**

## Already checked

Don't spend time here again unless a change touches it.

- **Aside:** strings for the in-place switch, the `aside_*` paths, the 8 Cocoa classes, its window tree on Windows, its memory with 10 tabs. It gets no input on the user's Mac: static reading only, or the VM.
- **Dia:** its window tree, with a page and with the collapsed sidebar floating over it (`Microsoft.UI.Content.PopupWindowSiteBridge`); its find bar has no window of its own. Launch, memory.
- **Chromium 158.0.8127.18 (2026-11-08):** `WebUIBrowserWindow`'s factory branch (`frame/browser_window_factory.cc:47-50`); `vertical_tabs.enabled` (`pref_names.h:1244`) and `views/tabs/vertical/`; `SetParentUiLayer` (`render_widget_host_view_mac.mm:405`); `NativeViewHostMac`'s four `NOTIMPLEMENTED` (`:254-271`); `kBrowserViewKey` and `kProfileKey` (`frame/browser_view.cc:1209,5040,5044`); the WebUI window's `ExtensionsContainer` (`webui_browser_window.cc:190-194`); Simple is the default HTTP cache on the Mac and not on Windows, and the backend experiment defaults to SQL; `GetBrowserViewForBrowser` at 158 call sites in 115 files outside tests; `BasicStartupComplete` before `PreSandboxStartup` and `PreBrowserMain`; `ui::AddScreenLockCallback` on every OS; views' ids exposed as `kHtmlId`; TSan only on Linux; PGO without an official build; no squircle, spring, or Acrylic in `ui/`. On 157 only: `TabStripModel` checks no profile at insertion (`tab_strip_model.cc:2369,4774`).
- **Materials:** the Swift UI uses no blur of its own, so Mica and Acrylic need no port.

## Testing as a person would

Scenarios prove the mechanics; a person checks what they see and do. The rig: cua-driver, `screencapture -l <window id>`, `screencapture -v` for motion, and the channel for setup only, never in place of the input under test.

- **Always a test copy,** through the scenario harness, on throwaway data, with its frame pinned (B0). Never the user's Pane, `/Applications`, the user's data, or their accounts.
- **Background first.** The front only when the user has said so in the session, or the Mac has been idle 10 minutes with its screen unlocked. Notify first, stop at the first sign of the user, and keep one test copy running at a time.
- **Before every key, prove the target** (memory `qa-never-drive-users-apps`).
- **In a VM, read the screen before typing into it** (memory `windows-test-vm`).

**The checklist, for K2 and again for W9, in light and dark:**

1. Launch, with and without a PIN, the PIN by real keys, a wrong one, the Lock at Lock Pane and at idle, time scaled.
2. Real browsing on throwaway profiles: public sites, the sign-in fixture surviving a relaunch and a Lock, a download, an extension from the Web Store, its popup, its shortcut.
3. Every overlay over a playing video: sheet, dialog, find, tab switcher, Lock. Keys go where they should.
4. Sidebar: reorder, resize, delete, 200 tabs, Sleep and Unload coming back.
5. Motion, recorded, against the Swift release, frame by frame.
6. Speed as felt: the same page loads and tab switches on both, timed from recordings.
7. VoiceOver or Narrator through the sidebar, toolbar, sheet, Lock.
8. The files from another app, with `strings` and `sqlite3`, on test data only.

## Working rules

- **The user's things:** never `/Applications` or their data root. Never input to the app in front. Never print their data. A secret never goes through chat, a log, or a process argument.
- **Builds:** niced, at `build.sh`'s 6 jobs (memory `keep-the-mac-usable`), one Chromium build at a time, on a Mac the user works on. Batch what rebuilds `base`. Long runs under `nohup`, with polls shorter than the tool's limit.
- **Before each commit:** AGENTS.md's gates, `gn check`, the OS-flag grep, `pane_unittests`, the scenarios touched, and G7's Windows compile. Bun only. `git status` first, and `git add` by path: other sessions may be running.
- **Commit each green item,** with its proof in the message or the log. Decisions go in the subject's ADR, findings in this log.
- **Upgrades:** 158 is in (0.19.0); take another only for a security need, by `engine/UPGRADING.md`, which runs `files-migration` against published builds. Count how much of `//pane` an upgrade touches, since that's this design's standing cost.
- **Design:** Paper first for anything shown differently (AGENTS.md; memory `paper-mcp-local`). Boards a change supersedes get redrawn, not left stale (memory `update-superseded-boards`).
- **Reference browsers:** parity of behavior, Aside first (memory `parity-with-stable-browsers`); never a competitor's signature color (memory `brand-color-not-competitors`).
- **Terms (`CONTEXT.md`):** update check, Theme, App icon, Sleep, Unload, Trim, Keep loaded, Save history, Lock, Data key, Engine key. Say "engine archive" for the prebuilt's key, which collides. Definitions naming the Mac or Dock get neutral at Phase 5.
- **Agents:** native subagents are welcome, for review especially. This plan authorizes no `sc` orchestration and no new worktree.
- **Language:** repo files in English; chat in the user's language. **The repo is public:** nothing of the user's goes in a commit, report, or screenshot.

## Gates that need the user

- **B6's numbers,** before 0.19.2 is published (D4).
- **The prototype's gate,** with numbers and a recording. Nothing past Phase 1 starts without it.
- **Every visible change, Paper first:** the surfaces U2 lists, Chrome's extensions pages inside Pane (D3), the text without tracking, and all of W5.
- **Publishing:** 0.19.2 (D4), the first Views release, and the first Windows alpha.
- **Accounts and money:** a certificate or the Store, a real x64 PC.
- **The threat model,** at U5, and if anything else would change it.
- **A budget or a data-loss risk that can't be met.**

## Risks

| Risk | What it would cost | What's done about it |
|---|---|---|
| Views can't match a surface (springs, text, squircle) | Craft, the thing this product sells | G5 and the primitives prove them early. Stop and ask rather than ship worse |
| Overlays over a page misbehave | The whole plan | G4, first thing, with a stop |
| More of `//pane` breaks at each upgrade | Time at every milestone, every two weeks, forever | Calls into `chrome/browser` are counted at each upgrade. The largest standing cost of U0 |
| A format slips | A locked-out or wiped user | C1's vectors and swap pass, before any screen exists |
| Chromium's code where Pane's was (Keychain, Local State, focus, menus) differs at an edge | A prompt, a stale record, a key going astray | The scenarios that cover each: `security`, `quiet`, `key-precedence`, `shortcuts`, and C1 |
| 158 `GetBrowserViewForBrowser` call sites | A null or CHECK at an upgrade | Same as today; scenarios catch it |
| CI gets weaker (U4, D1) | A red `main` goes unseen longer | Local gates before push, and a builder later |
| The freeze runs long (U3) | Features wait | The plan's pace; the meter shows it; data-loss fixes go on |
| Windows writes plaintext silently (W2) | The product's promise | Refuse unregistered paths; the disk check with a control |
| Pace is unknown | These estimates | They're wall clock from earlier plans, which ran far under, but none of that work was this visual: treat them as a floor |

## Effort

Wall clock, unmeasured. The encryption plan estimated 9 to 10 weeks and took 16 hours; the native Mac app took 11 days from first commit to 0.18.0. This work is more visual, and each surface needs eyes. Builds and scenario runs fill much of it, in the background.

| Phase | Time | Depends on |
|---|---:|---|
| 0 Tools and baselines | a day, plus B6's build | — |
| 1 Prototype and gate | 3 days | 0 |
| 2 Model and data | 2 days | 1 |
| 3 The surfaces | 8 days | 1, 2 |
| 4 Cutover | a day | 3 |
| 5 Windows | 1 to 2 weeks, with waits | 4 |

About three weeks for the Mac, and five for both, if the prototype passes. Windows could start beside the surfaces if the user would rather have the alpha sooner, since most of what's left in it doesn't touch the UI.

## Log

- **2026-11-07, written.** Four read-only reports (the Swift app, the engine and patches, the ADRs, tests, and CI, and Aside and Chromium's seams), each given the code, not conclusions, plus the VM's measurements of Dia, Aside, and Chrome on Windows. The author confirmed by reading the scenario count (77), the line counts, Aside's in-place strings, the `WebUIBrowserWindow` branch, and the vertical-tabs pref. Nothing was built or run for this plan. Two claims changed the plan: Aside overwrites `Browser::profile_`, which the brief assumed it did not, so U1 exists; and the migration cuts few patches, since 90 of 123 edited files are product behavior, so the plan doesn't promise a much smaller patch series.
- **2026-11-07, the decisions.** The user accepted every recommendation, U1 to U8, and the list of what `review-and-simplify.md` skips. That plan's session is in its Phase 7, so B1, B2, B3, and B5 wait for its release; B4's VM half starts now.
- **2026-11-07, B4's VM half.** Visual Studio Community 18.11.3 (13 minutes, MSVC 14.51.36231) and the Windows SDK 10.0.28000.2526 (2 minutes), in the VM at 3 cores, beside the other session's work. `package_from_installed.py 2026 -w 10.0.28000.0` gave `dcda99cac7.zip`, 2.4 GB, 8.5 GB unpacked, with x86, x64, and arm64 and the debuggers it requires, kept in `~/pane-windows-vm.noindex/toolchain/`, outside the repo. Copied out with the same SHA-256. The VM went back to its snapshot, since a cross build needs only the zip. **Its second half waits:** `target_os = ['win']` in `.gclient`, then `gclient sync` with `DEPOT_TOOLS_WIN_TOOLCHAIN_BASE_URL` at that folder and `GYP_MSVS_HASH_<TOOLCHAIN_HASH>=dcda99cac7`, where `TOOLCHAIN_HASH` comes from `build/vs_toolchain.py` (`3bfcb536c8` on 157; read it again on 158). **Claims wrong or missing:** the docs ask for SDK 10.0.28000.2270, and winget's latest is 2526; the docs say the x64 debuggers don't install on ARM64, but this SDK installed them. Embedded Python's `._pth` leaves the script's folder off the path, so `get_toolchain_if_necessary.py` goes beside `python.exe`. Winget's manifests can carry a carriage return in their URLs: `aka.ms/vs/18/stable/vs_community.exe` is the plain link.
- **2026-11-08, the review plan's handoff.** Its session reported what it finished, and the repository confirms it: `main` at `8bee4e5`, 0.19.1 `latest`, Chromium 158.0.8127.18, the engine archive `46b8f8b35c24` in `prebuilt-engines.tsv` and in `out/Release`, ADR 0122, `KeychainKeyProviderUserDataDirProtection` off, and its leaks into `$TMPDIR` fixed (`package.sh`'s entitlements folders, `PersistenceTests`' folders). What changed here: every upgrade path stays, so the data phase gained its upgrade paths and half a day, and Done when's second line follows ADR 0122's table; E6, E7, E9, E12, E14's `@synthesize`, and E15 are done, so the patch targets were set again from 93 files and 1,034 lines; the counts were taken again at `8bee4e5` with the commands now under "Where things stand", since the review plan that held them is gone; 158's facts were checked again ("Already checked"). **Claims wrong or missing:** the first count's "157 sites" of `GetBrowserViewForBrowser` read like Chromium's version; on 158 it is 158 call sites in 115 files outside tests. 158's `build/vs_toolchain.py` asks for 157's `TOOLCHAIN_HASH` and SDK, so B4's zip holds.
- **2026-11-08, the review session's reading of this plan,** asked by the user: read only, nothing built or run. The author checked each claim that changed the plan against the code and Chromium 158: `main.swift`'s order, the moved lines, `pin.hash` and `recover`, the watchdog's calls, `CreateHttpAuthCoordinator`, `LoginTabHelper`, the hook in `browser_window_features.cc`, where `set_view_factory_for_testing` exists, `report.md:60`, `browser.ts:107`, and `engine-key.sh`'s sources. What changed: B0, since test copies share the user's preferences; B1 and B2 start from nothing kept, with the commands to make them; `dev.gn` left `engine/args`; the switch gained its key formula and a guard; the oldest golden roots came from fixtures and by hand; C2's `profiles.json` defaults and `security.json`'s `hash` and `recover`; the vault's wipe, Keychain, and watchdog; the start's whole order; the upgrade paths' smaller steps and timing; Chromium 158's memory is 1.0 to 1.4%. **Claims wrong:** this plan's, that a stub `SidePanelUI` removes `browser_window_features.cc` (it makes Pane's `WebContentsDelegate`, and stays) and that `set_view_factory_for_testing` could replace the external-app, install, and permission hooks (the first two have none, and the permissions' one is for tests, which presubmit keeps out of production), so the patch targets are ≤ 89 files and ≤ 990 lines, not ≤ 86 and ≤ 930; that `dev.gn` could sit in `engine/args`; the handoff's "about 2%" for 158's memory, from an earlier set.
- **2026-11-08, seven readers, for simplicity,** asked by the user: as little complexity as possible, every feature, and performance kept and improved. Each read the whole plan through one lens (architecture, process, data, UI, performance, Windows, and a skeptic's), read only. The author checked each claim that changed the plan: `.ninja_log`'s last incremental build, TSan's Mac assert, `sync.sh`'s `target_os` and `-D`, `release.yml`'s tag check, PGO's profile and its lack of an official-build assert, `AfterFrame`'s callers and numbers, `PageHostView`'s circular corner, `window-wait`'s match, the benchmarks with no scroll, `BasicStartupComplete`'s place, `AddScreenLockCallback`, views' `kHtmlId`, the cache experiment's SQL default, `PinHash.make`'s callers, the key router's size, and the window registries. What changed: twelve phases became six, the design system and the channel folding into the surfaces and the prototype, and the cutover's steps spreading into Phase 0 and the surfaces; B3's component build, the `DEPS` files and four layers, the key router, the gallery, G7's full Windows build, and the stored golden roots went; Chromium's own code replaces Pane's for the Keychain, Local State, menus, accessibility ids, window lookup, and Lock triggers; start moved to `BasicStartupComplete`, the vault to task sequences, ADR 0122's paths to a Mac-only target; the gate measures input by signposts and scroll by trace, and holds 0.19.0's memory and unlock budgets; B6 adds PGO. Four questions went to the user (D1 to D4). **Claims wrong:** this plan's, that `AfterFrame` needn't be ported (it saves 9.7 to 15.4 ms per switch and new tab); that `DEPS` files would enforce the layers; that `base::File::Lock` can take `Pane.lock` (it is `fcntl`, where published Panes `flock`); that `ui::SessionChangeObserver` serves both OSes (Windows only); that the cutover could run TSan on a Mac; that the live update could run before publishing; that start could run in `PreBrowserMain` (a 0.13.1 user would start empty). **Found in the product:** `SecurityState.recover` missed every hash 0.13.1 wrote, since Swift's JSONEncoder escapes `/` as `\/` and bcrypt's alphabet holds `/`, so a damaged `security.json` from then kept the Lock shut with no PIN to check and no wipe due. Fixed in `Security.swift`, with a test (`recoverEscapedSlashes`) and the old code's miss shown, uncommitted, for D4's 0.19.2.
- **2026-11-08, the user's answers.** D1, D2, and D4 as recommended: Swift ships from a `swift` branch off `v0.19.1` while `main` becomes the Views line; the tree moves first; a PGO engine goes out as 0.19.2 with the `recover` fix if it measures faster and the user has seen the numbers. D3 against the recommendation: Chrome's extensions page and shortcuts page replace Pane's, Paper first, so X4 and X5 shrink to wiring and scenarios, and the product-lines target falls to about 37,000.
- **2026-11-08, Phase 0 starts.** The counts again at `8bee4e5`, with the `recover` fix in the tree: Swift 22,926 (the fix's 2 lines), `.mm` 7,929, `engine/pane` and C 18,654, so product lines 41,580; the patches edit 93 Chromium files in 1,034 lines and add 9; tests are 285 PaneKit (with `recoverEscapedSlashes`), 63 `pane_unittests`, and 81 scenarios. AGENTS.md's gates pass on that tree: lint, typecheck, knip, and `swift test`'s 285. **Claims wrong:** ADR 0004's banner named K6 for its rewrite, which is K3; `sync.sh` runs `gclient sync -D` once per Chromium version, behind its `.synced-<version>` marker (`sync.sh:56-59`), not on every run, so B4's `target_os` takes the marker out as well.
- **2026-11-08, B5.** `swift` is cut from `v0.19.1` with the `recover` fix (`9ea90d2`, the same change as `main`'s `d42cfc4`), and its own commit teaches its `release.yml` to take a tag on `main` or `swift`, runs `app.yml` and `gates.yml` on its pushes, and names it in its AGENTS.md. On `main`, `app.yml` and `setup-app-build` are gone: `release.yml` installs XcodeGen and fetches the engine itself, without the cache (a release rarely meets one), and takes a tag on `main` or `swift`; AGENTS.md names the exception and the scenarios run on this Mac before a push; ADRs 0049 and 0086 say so at their tops; `actionlint` passes on every workflow. With `app.yml` gone, knip reported `ScenarioFailure`'s export in `tests/run.ts`, which nothing imports, so it is no longer exported.
- **2026-11-08, B0.** A scenario's copy (`TestHooks.enabled`) now opens at 1280 × 800 every launch and sets no autosave name, so it neither reads nor saves `NSWindow Frame PaneMainWindow`; the Views build keeps that rule. Proved without the user's key, on copies re-signed under a throwaway identifier, so each wrote its own domain: after `smoke`, the published 0.19.1 had saved `116 111 1280 800 0 0 1512 949` (its first show centers the window, and the autosave writes it), and the fixed build had saved nothing; given a frame of 1600 × 900 beforehand, the fixed build left it as it was. The domains were deleted after. **Claim wrong:** the plan's "read that one key of the user's, before and after": the throwaway identifier proves it with a control and reads nothing of theirs. **Found:** every scenario's launch until now saved the user's frame, centered, at its first show, and `__CFPREFERENCES_AVOID_DAEMON` isolates nothing (a probe read the real domain through it and its writes landed nowhere). The fix goes on `swift` too, so 0.19.2, B1's baseline (D4), carries it.
- **2026-11-08, B4's second half.** `sync.sh` names `target_os = ["mac", "win"]` and `checkout_pgo_profiles`, re-syncs when `.gclient` changes, and takes the toolchain from `PANE_WIN_TOOLCHAIN_DIR`. One run, about 20 minutes beside other work: the zip unpacked into `src/third_party/depot_tools/win_toolchain/vs_files/dcda99cac7`, `build/win_toolchain.json` names it, the PGO profiles for `mac-arm`, `mac`, `win64`, `win32`, and `win-arm64` are in `chrome/build/pgo_profiles`, and `apply-patches.sh --check` still passes. 158's `TOOLCHAIN_HASH` is `3bfcb536c8`, as the plan said. A Windows `gn gen` waits for G7, since `engine/pane/BUILD.gn` still asserts `is_mac`.
- **2026-11-08, B2.** `run.ts --appearance light|dark` hands Pane's launches `PANE_SYSTEM_APPEARANCE`, which a scenario's copy takes for the system's appearance (the Theme's System, the Lock's included, before the settings open), since a scenario can't change the Mac's. The 11 scenarios that call `app.render` ran in both, on a package of the Swift app at `swift`'s UI with the published engine, and all passed (`quit` and three checks of others deferred for the front): 57 renders a side, 2560 × 1600, in `evidence/views-migration/renders/swift/{light,dark}/`, the Lock dark in the dark run. Scenarios that set the Theme themselves render it as they set it. Still to add before Phase 3: the surfaces no render reaches (the list goes with each surface's port, from the Swift while `swift` holds it). **Checked:** Chromium 158 shapes with no OpenType features (`render_text_harfbuzz.cc:1471`, `hb_shape(…, NULL, 0)`) and spaces glyphs only for obscured text, so tracking and tabular digits have no Views path, as the plan said; their 9 uses (4 tracking, 5 `monospacedDigit`) could keep their look through a label of `//pane`'s that shapes with `tnum` and draws through Skia, an option to weigh at G5 before the Paper board.
- **2026-11-08, B1's tools.** Tracked now: `tests/ab.ts` runs two binaries' benchmarks in turns, the base first in odd rounds and the candidate in even ones (`bench2.sh` always ran the base first, so the later build took any rise in load), and writes `compare.{json,md}` from every `median`, `p95`, `medianMs`, and `p90Ms` leaf, a failed run reported in its table; `tests/tools/image-diff.swift` is `pngdiff.swift` with an optional image of the pixels that differ; `window-wait` requires `kCGWindowAlpha > 0`; `benchmark-memory`'s summary gives each process type, the browser's among them; `benchmark-scroll` scrolls `pages/scroll.html` with `Input.synthesizeScrollGesture` (one wheel gesture, an event per vsync, scrolled on the compositor thread) and counts the scroll's frames from a `benchmark` trace. On the Swift build, beside a Chromium build (load about 10): 467 presented, 12 partial, 1 dropped of 480, ±1 or 2 between runs. **Claims wrong:** `window-wait` didn't require alpha yet; `byType` was only in the raw snapshots; the scroll benchmark is about 110 lines of code, not 50; a main-thread miss reports a frame twice (`FORKED`), so frames are merged by sequence, worst state first, as `cc/metrics/frame_info.cc` does. Under that load, one binary against itself differed by up to 19% on time to window in one round: B1 needs the quiet Mac and three rounds.
- **2026-11-08, Paper for U2 and D3, waiting for the user.** A new page, "10 · Views migration", holds 10.1 to 10.3 (Chrome's extensions list, details, and shortcuts page in Pane's window: the engine's own captures of `chrome://extensions` in a profile's tab, 1012 × 741 pt, composited into the Swift render), 10.4 and 10.4.1 (the page's context menu, Pane's beside Chrome's for the page, a link, selected text, and a field), and 10.5 (the action popup, the `NSPopover` beside Chrome's `ExtensionPopup`), each in light and dark, with what a person would notice. Nothing existing was touched. **Found:** Pane never runs `RenderViewContextMenu` today (`HostedWebContentsDelegate::HandleContextMenu` hands it to Swift), and Chrome's brings items Pane has no use for (Incognito, New Window, Split View, Open Link as Person N, Lens, Reading Mode, QR), none of which a pref turns off: hiding them needs a filter on the menu's model or a patch. `ExtensionPopup` turns its rounding off (`extension_popup.cc:273`), so it would be Pane's one square corner. Chrome's details page shows "Allow in Incognito" unless incognito is off by pref, which Pane doesn't set.
- **2026-11-08, B2's surfaces, and B5's CI.** `tests/rigs/surfaces.ts` renders by name, in the run's appearance, the 51 surfaces and states the scenarios' renders miss: the window with two profiles (a sleeping, a loading, and a selected tab, the proxy dot's three states, an unloaded profile), a drag, the sidebar's fades, the toolbar loading and focused, find, the switcher, every page dialog (alert, confirm, prompt, reload, leave, resubmit, permission, external app, sign-in, proxy sign-in, hung page), the sad tab, the Extensions page's empty states, Quit Pane? (from the menu, without the front), Settings top and end, its update and App icon states, the App icon picker, every PIN screen, the Lock's states, and the update states. Both appearances passed (110 s a side), 51 renders each, the same names, on the Swift UI; `surfaces.json` names what no render reaches and why: menus and sheet windows (`render` draws the window's host view only), hovers (the channel's synthetic move reached a tracking area once in three runs), web content, system panels, the device chooser (Swift declines every request), and states that need the network or an earlier build. The address field typed in and the fullscreen hint wait for the front. CI on the pushes: App and Gates pass on `swift`, Gates on `main`.
- **2026-11-08, B6's build.** `release.gn` takes `chrome_pgo_phase = 2`, read from the `mac-arm` profile `sync.sh` fetched (`chrome-mac-arm-8127-…`). One full build at 6 jobs, beside other work: 141 minutes, not the 3.5 hours the plan said, engine key `b8eb8b0e…`, which `swift` will have too, since its engine sources are `v0.19.1`'s with this line. Committed on `main` now so the tree can move (B7); its numbers against 0.19.1 decide whether it stays (D4).
- **2026-11-08, B3 and B7.** B3: one edit in `//pane` (`browser_main_extra_parts.mm`, its time only) rebuilt `out/Release`'s `chrome` in 22.6 s at 6 jobs, PGO on, under the gate's 30 s before a window opens, so no component build. The framework is 260.6 MB stripped with PGO against the published 0.19.1's 274.2 MB. B7: `engine/pane` is `pane/`, `engine/scripts` is `scripts/`, and the rest of `engine/` is in `chromium/`, by a script that `git mv`s them and rewrites every path naming them outside what the engine key hashes, tried first on a clone; `src/pane` links to `pane/` again (`sync.sh`), `apply-patches.sh --check` passes, and the engine key is `b8eb8b0e` before and after. ADR 0086 records the tree. **Claims wrong:** "the move keeps the engine key" holds only if the files it hashes stay byte for byte: rewriting `pane/browser/ui/hooks.h`'s comment, which names `engine/patches`, changed it, so that comment waits for the next `//pane` change; the plan's "72 files and 192 lines" counted otherwise: 74 files named the paths, 60 changed, beside the 182 moved.
- **2026-11-08, B6's numbers, for the user (D4's gate).** The PGO engine against the published 0.19.1, three rounds of every benchmark taking turns, then five of unlock alone (`docs/benchmarks/README.md`): page work is faster (Apple's cold first paint −21.7%, GitHub's cold load −10.9%, the fixtures 5 to 17%), memory is 1.5 to 3.2% lower (the GPU process −14.5% at ten tabs, the browser process the same), the framework 5% smaller, and launch, unlock, stores, and scroll are unchanged within their noise (PIN to page +10.2 ms over five rounds, 8.6 of them the volume's attach). Its archive (`PaneCore-158.0.8127.18-b8eb8b0eb2fb.zip`, SHA-256 `457ec11c…`) and dSYM are made and kept in `app/release`, and its bundle in `evidence/`, so `out/Release` can build the prototype; nothing is published. **Found:** a build run from the scratchpad (`/private/tmp`) starts no volume watchdog, where the same build run from the repository does: candidates run from `evidence/`. The first three rounds' unlock read +32 ms; five read +10.
- **2026-11-08, Phase 1 begins (G1 to G6 and G8, first pass).** Before it, `//pane` is formatted with Chromium's `clang-format` in a commit of its own (`1ad7b73`, 62 files). What the first pass holds: `PaneMainDelegate::BasicStartupComplete` starts Pane's Views UI when Chromium's loader starts the engine bare (no `--pane-hosted`), only as a scenario's copy for now (the mock keychain and `PANE_DATA_DIR`, refused otherwise, so it never opens the user's data until the vault's port); the engine's command line comes from one function both launches call (`pane/app/engine_switches`), and a profile opens through one C++ path (`pane/browser/profiles`); `WindowHost` takes a C++ delegate beside the Swift app's Objective-C one, its Objective-C fields behind a pimpl so `.cc` files can include it; the Views window (`pane/ui/window`) hosts each profile's hidden Browser over one `views::WebView` whose page has layer corners (`SetNativeViewCornerRadii`, fast rounded corners), a sidebar of both profiles' tabs, and an address field; the frame (`frame_mac.mm`) is a `NativeWidgetMac` whose `NSWindow` and title-bar frame put the lights at (12, 12) through AppKit's own overrides, as Chrome's browser window does, and press areas return `HTCAPTION`; presses over a page that a view covers go to Views (the window's `sendEvent:`); ⌘T and ⌘L go before the page, ⌘1 to ⌘9 after it, through the `FocusManager`; a motion driver steps layer opacity and transform on compositor frames (cubic and SwiftUI's spring); the channel watches its folder (`base::FilePathWatcher`) and answers `state`, `createProfile`, `openTab`, `select`, `menu`, `render` (software paint, the page left out), the sheet, and `quit`; `Select tab`, `New tab`, and `Focus address field` are signposted as the Swift app's are, ended at the compositor's commit, where the Swift app's end at Core Animation's. `tests/rigs/views-prototype.ts` passes on it: two profiles, a page each, the switch, the sheet over the page, ⌘T from the menu. Gates: `gn check` over `//pane` passes for the first time (it had 52 errors in 24 files: 23 deps added, `browser.h` included with `nogncheck`), `pane_unittests` 63 of 63, and on the Swift app over this engine `smoke`, `hosted`, `quiet`, `navigation`, `profiles-isolation`, `restore`, `open-urls`, `security`, `crlset`, and `files-encrypted` pass. **Claims wrong:** the plan's "`gn check` runs before each commit" assumed it passed; it never had. **Not yet:** G1's bundle, G3's VoiceOver and live resize, G4's proof over a playing video, G7, and the gate's numbers.
- **2026-11-08 20:00, the Mac panicked, by this session's doing.** `watchdog timeout: no checkins from watchdogd in 90 seconds`, as ADR 0007's first build did, with the Windows VM (4 vCPUs, 8 GB) left running beside a 6-job Windows compile, minutes after about 25 GB of toolchain cloning, hashing, and unzipping. A 6-job build alone had run 141 minutes that afternoon: the stacking did it. From here on one heavy job runs at a time (a build, the VM, benchmarks, a big disk job), and the VM stops the moment its task is done. Nothing was lost: `git fsck` is clean, the patches stay applied, the PGO engine's archive matches its SHA-256, and the VM's disk went back to `browsers-installed` and checks clean. `/private/tmp` is cleared at boot, so working files live in `evidence/` now, not the session's scratchpad. **Claim wrong (B4's log):** the toolchain zip held no x64 debuggers (`Windows Kits/10/Debuggers/x64`), which even an arm64 target's `gn gen` copies; they came from the SDK's `X64 Debuggers And Tools` MSI, extracted in the VM without installing, and the toolchain is now `afaa3b7413` (the old one plus them, hashed as `package_from_installed.py` hashes, which gives `dcda99cac7` back for the old). With it, `gn gen out/Win` (x64) passes, and `gn gen out/Win --root-target=//pane` loads `//pane` alone, which `gn check` passes on Windows too, once `pane/BUILD.gn` keeps its Objective-C and Mac files behind `is_mac` and drops `assert(is_mac)` (G7).
- **2026-11-08, the user's answer on Paper 10.5: the extension popup keeps Pane's look.** Today's popover (Paper 04.12: rounded corners, the arrow at the toolbar's button, the frame around the page) stays, over Chrome's stock `ExtensionPopup` (square, no arrow): U2 changes for the popup, so X2 hosts the extension's page in a bubble of `//pane`'s in Views drawn to 04.12, and ADR 0057 says so when X2 lands. 10.1 to 10.4 (Chrome's extensions pages and the context menu) still wait.
- **2026-11-08, the user's answers on Paper 10.1 to 10.4, and on 0.19.2.** 10.1 to 10.3 approved: Chrome's extensions pages open in the profile's tab, with "Allow in Incognito" hidden (Pane has no incognito); Developer mode and the Web Store's link stay. 10.4 and 10.4.1: Chrome's context menu, everything kept but what Pane can't support, as incognito, since a profile is already apart: "Open Link in Incognito Window" goes, with what needs Chrome's own browser window, which Pane's lack (New Window, Split View, Lens, Reading Mode, QR); "Open Link as" is kept only if it can name Pane's profiles. The user approved publishing 0.19.2: `swift` took the PGO args (`b86aa75`, engine key `b8eb8b0e`, as built), `engine-b8eb8b0eb2fb` is a prerelease with its archive and dSYM, `prebuilt-engines.tsv` names it (`4ed61a6`), and `v0.19.2` is tagged on `3a802d8`. Before the tag, the candidate passed `smoke`, `hosted`, `quiet`, `navigation`, `profiles-isolation`, `restore`, `open-urls`, `security`, `crlset`, and `files-encrypted`.
- **2026-11-08, G7.** `//pane`'s 20 common `.cc` files compile for Windows x64 (`out/Win`, `chromium/args/win.gn`, `gn gen --root-target=//pane`), the Views UI and the channel among them: `pane/BUILD.gn` builds its Objective-C and Mac files under `is_mac` only, `assert(is_mac)` is gone, and `gn check` passes over `//pane` on both. What Windows found: paths built from narrow strings (`FilePath` holds UTF-16 there, so `AppendASCII` and `FromUTF8Unsafe`), `NAME_MAX` (a constant of 255 now, and `\` refused in a profile's identifier on every OS, so the rule has no OS in it), and the engine's switches as a list of `std::string`: `SetEngineSwitches` now works on a `base::CommandLine` in place, for both launches. The Swift app over this engine passes `smoke`, `quiet`, `open-urls`, `profiles-isolation`, and `security`; `views-prototype` passes; `pane_unittests` passes. Before each push from here on: `scripts/` has no step for it yet, so the commands are in this entry's commit.
- **2026-11-08 23:23, 0.19.2 is published and `latest`** (CI's Release, App, and Gates passed on `3a802d8`; the App run on the intermediate `b86aa75` failed as an engine change does before its prerelease is named). The real update, against GitHub, from a copy of the published 0.19.1 (`evidence/review-2026-11-06/rig/zz-update-live.ts`): found 0.19.2, downloaded it in 63 s, staged it, swapped at quit, and the swapped copy opened the same data (the page's value read back, the volume there). Per D4, 0.19.2 is B1's baseline: the gate's A/B measures it beside the prototype on the same Mac and day, with `tests/ab.ts`, so no separate baseline run is kept.
- **2026-11-08, the prototype under the gate's instruments.** It opens what it's launched with (`Pane https://…`) as the Swift app does, behind, in the profile in use, "Personal" made on first launch, and its sheet carries the platform tiles and the signposts `interaction-timing` reads, so both benchmarks run on it unchanged: `benchmark-scroll` gave 468 presented, 12 partial, 1 dropped of 480 on the compositor thread; `interaction-timing` gave, at medians, a tab switch 14.9 ms, a new tab 28.6 ms, the address field 7.1 ms, the sheet 12.5 ms. **Claim refined:** its intervals first ended at the compositor's commit, as the Swift app's end at Core Animation's, and the address field read 0.4 ms, a commit already under way; they now end at the next frame's presentation (`RequestSuccessfulPresentationTimeForNextFrame`), as the plan said, which the Swift app's never reached, so the gate's A/B reads the Views build later by the pipeline to the screen, and the recording at the display's rate is what compares them fairly.
- **2026-11-08, G8's runner half.** `tests/lib/pane.ts` waits for a reply by watching the channel's `out` folder (`fs.watch`, a 250 ms look behind it in case an event goes missing), not a 50 ms sleep, and the Views app watches `in` (`base::FilePathWatcher`): `views-prototype` went from 6.3 s to 3.1 s. The Swift app, which still polls every 100 ms, passes `smoke` and `profiles-isolation` on it.
- **2026-11-08, G6's keys, built and waiting for the front.** The channel's `key`, `holdModifiers`, and `releaseModifiers` post real `NSEvent`s into the app's queue for the window (`pane/automation/input.h`, `input_mac.mm`), as the Swift app's do; the state reports `focus` (address, page, sheet) and `address`; a tab chosen gives the page the keyboard, and ⌘T gives it to the address, as Chrome does. `tests/rigs/views-keys.ts` checks ⌘T and ⌘L ahead of the page, a key queued behind ⌘T landing in the new tab's address, ⌘1, ⌘3, and ⌘9, and Esc and Return closing the sheet: it needs the window in front, so it waits for the user's go or an idle Mac. `gn check` passes on both platforms, and the common objects still compile for Windows.
- **2026-11-08, G4, in the background.** `tests/rigs/views-sheet-over-video.ts` opens the sheet over a page playing video (`pip.html`): with no sheet, a press in the page's middle goes to the page (the platform hands it `RenderWidgetHostViewCocoa`); with the sheet, a press on its scrim or its card goes to Views (`BridgedContentView`), and the frames of its animation in and out held: 47 presented in full, none dropped, while the video played. The channel's `hitTest` answers who takes a press with no input, so this runs without the front; real clicks and keys into the sheet wait for it (`clickWindow` now posts real clicks). **Claim wrong:** the plan's "NativeViewHostMac leaves clips and hit-test insets NOTIMPLEMENTED, so this is the first thing to prove": Chromium's `BridgedContentView` already asks Views who is at a point before AppKit picks a subview (`bridged_content_view.mm:367-409`, `native_widget_mac_ns_window_host.mm:1461`), so a view over a page takes the press; the window's `sendEvent:` routing written for it was removed unused.
- **2026-11-08, G1's lights.** The frame (`frame_mac.mm`: a title bar `2 × 12` plus a light's height tall, the lights centered in it, 12 from the left, through AppKit's own overrides as Chrome's browser window does) puts the traffic lights at (12, 12), evenly spaced: `views-prototype` now checks `state.window.trafficLightFrames` with `trafficLightsInPlace`, the Swift app's check (`window-chrome`), and passes. `WindowControlFrames` reports them; on Windows it will report the caption buttons.
- **2026-11-08 23:45, with the front (the user's go, for 10 minutes).** `views-keys` passed: ⌘T and ⌘L went ahead of the page, an `a` posted right behind ⌘T landed in the new tab's address, ⌘1, ⌘3, and ⌘9 chose by place, and Esc and Return closed the sheet (G6). `views-sheet-over-video` passed its front half: a real click on the scrim never reached the page, and a key stayed in the sheet (G4). The felt check (`frame-timing.ts`, ⌘T recorded on both builds) waits: after the reboot the cua-driver daemon needs its Accessibility and Screen Recording grant confirmed (`cua-driver permissions grant`), which only the user can click. The prototype now also starts from a scenario's derived data root (a home of its own, `qa-hold`'s), and refuses any data folder outside the temporary folder, which is checked: launched bare it logs its refusal and exits.
