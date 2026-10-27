# Native Shell on Pane's Own Chromium Framework (Arc/Dia Architecture) Implementation Plan

> **For autonomous runs:** execute Phase 0 through Phase 7 in order, unattended, from start to finish, **never stopping to ask the user**. Tasks use checkbox (`- [ ]`) syntax; tick a task only when it is done and its evidence exists. The **Acceptance** list of a phase is its completion audit: a phase is complete only when every criterion has the named evidence on disk under `native/evidence/phase-N/`. **[HUMAN]** items never block. Follow the Autonomy policy below.

**Goal:** Replace the Electron app (`apps/veil`) with a native macOS browser built the way Arc and Dia are built: a pure-Swift app (AppKit + SwiftUI) that links Pane's own Chromium build, `PaneCore.framework`, in-process, through an Objective-C embedding API. Reach feature parity with today's Pane, then cut over.

**Architecture:**

```
Pane.app (Swift: AppKit + SwiftUI): sidebar, profiles, tabs, toolbar, lock screen,
   │                                settings, every dialog and prompt
   │  Objective-C API (PaneBrowserApplication, PaneBrowserContext, PaneWebContents, …)
   │  + delegate protocols (engine → app callbacks)
PaneCore.framework = Chromium's chrome/ layer + Pane's //pane tree
   ├─ //pane/browser/*_impl.mm   embedding API implementation
   ├─ //pane/browser/ui/*        Pane implementations of Chrome's UI hooks (BrowserWindow,
   │                             permission prompts, JS dialogs, login, context menu, …)
   ├─ //pane/renderer/*          per-renderer agents (fingerprint, page-level features)
   ├─ //pane/common/*.mojom      browser ↔ renderer interfaces
   └─ native/engine/patches/     small Chromium patches: framework layout, host mode, hook seams
Pane Helper (Renderer | GPU | Plugin | Alerts).app: normal Chromium child processes
```

- **One process, like Arc:** the app's `main` calls `ChromeMain`; Chromium owns the run loop.
- **Tabs are NSViews:** the app inserts each tab's `nativeView` into its own view hierarchy, so tabs from several profiles can live in one window.
- **Chrome's `Browser` objects exist but stay hidden:** a Pane implementation of Chrome's `BrowserWindow` interface backs each window, so extension APIs, `TabStripModel`-based features, and Chrome internals keep working with no Chrome UI on screen.
- **Every Chrome-owned UI surface becomes a delegate callback** rendered natively by the app.

**Tech Stack:** Chromium (chrome/ layer) at a pinned stable tag; C++ and Objective-C++ in `//pane`; an Objective-C public API with nullability and `NS_SWIFT_NAME`; Swift 6, AppKit, SwiftUI; XcodeGen; Swift Testing; depot_tools, gn, autoninja; Bun scripts for the scenario runner (CDP client and trace checks).

## Decisions (fixed — do not re-litigate)

1. **Arc/Dia architecture:** in-process framework (`main` → `ChromeMain`, tab NSViews in the app's hierarchy). Not the out-of-process OWL design (Atlas).
2. **Built on Chromium's `chrome/` layer**, not `content/`. Not CEF, not Electron, not Tauri or wry, not WKWebView. arc-engine's analysis found CEF covers only 167 of the 1,701 methods Arc's app needs, because embedding in the app's NSView forces Alloy style (no extensions, autofill, omnibox, Chrome dialogs).
3. **The UI is pure Swift** (AppKit + SwiftUI). No web UI for browser chrome: no React, no React Native, no WebUI pages for Pane's own surfaces.
4. **macOS on Apple Silicon only.** Windows and Linux are out of scope for now.
5. **Engine code lives in `//pane`.** Patches to Chromium stay minimal (framework layout, host mode, hook seams). `native/engine/patches/MANIFEST` maps every touched Chromium file to the patch that owns it.
6. **The Electron app keeps shipping, untouched,** until the Phase 7 cutover.
7. **Product language and design rules carry over unchanged:** `CONTEXT.md` (Sleep, Unload, Trim, Keep loaded, Update check, Theme), `DESIGN.md`, ADR 0001 (theme drives the whole app and web content), ADR 0002 (update check via GitHub Releases API).

## References and how each may be used

| Reference | License | Use |
|---|---|---|
| [Ardia](https://github.com/mantrakp04/ardia), `engine/patches/` (63 patches on Chromium 156.0.8078.47) | Distributed GPL-3.0; own sources Apache-2.0 | **Patches may be reused.** Check each patch's own license, record its origin in `native/NOTICE`. Most relevant: `chromium-ardia-layer`, `chromium-hidden-window-current-window`, `chromium-extension-window-hidden`, `chromium-hosted-browsers-no-session-service`, `chromium-extension-install-prompt-hook`, `chromium-password-bubble-hook`, `chromium-autofill-address-bubble-window`, `chromium-device-chooser-hook`, `chromium-side-panel-hook`, `chromium-safe-storage-name`, `chromium-branding` |
| [arc-engine](https://github.com/CryogenicPlanet/arc-engine) (`ENGINE.md`, `specs/`) | None; derived from decompiling Arc's proprietary binary | **Reference only.** Read it for the shape of the API (146 classes, ~270 callbacks) and the bundle layout. Never copy code, patches, or specs text |
| [arkt](https://github.com/krystianslowik/arkt) | MIT | App-shell structure (XcodeGen, shell/UI package split) |
| [Helium](https://github.com/imputnet/helium) | GPL-3.0 / BSD-3 | De-Googling and branding patches if needed (Ardia already carries several) |
| [fingerprint-chromium](https://github.com/adryfish/fingerprint-chromium) | BSD-3 | Blink-level fingerprint patch reference |
| Brave `brave-core` farbling | MPL-2.0 | Canvas/audio/WebGL noise reference |
| CEF source | BSD-3 | Reference for how hooks into `chrome/` are implemented |
| OpenAI "How we built OWL" (Atlas) | — | Fallback design only, if in-process proves unworkable |

## Autonomy policy

The run is unattended: the user is away or asleep. Never pause for input.

- **[HUMAN] items are deferred, not awaited.** Do every automatable part of the item and save its evidence. Then add an entry to `native/HUMAN-CHECKLIST.md` with: phase, item, why it needs a human, exact steps, and the evidence file the result should produce. The criterion counts as met for the phase audit once both the automated evidence and the checklist entry exist.
- **Never act on the user's identity:**
  - no sign-ins to real accounts, no invented credentials;
  - no purchases, subscriptions, or account creation;
  - never typing secrets found on the machine.
- **When technically blocked,** try the alternatives first: another hook point, another patch approach, the Chromium/CEF/Ardia sources. Then pick the option most aligned with the Decisions, record the choice and the reasoning in `native/DECISIONS.md` (a dated engineering log), and keep going.
- **Never pass a criterion by weakening it.** A feature may be dropped only after the alternatives fail. The drop needs a technical justification in `parity.md` and an entry in `HUMAN-CHECKLIST.md`.
- **Publishing stays with the user.** No `git push`, no tags, no GitHub releases, no uploads. Phase 7 ends with the release prepared locally, and its publish commands written in `HUMAN-CHECKLIST.md`.
- **Keep the Mac awake** for the whole run (`caffeinate -dimsu` in the background).
- **Long jobs run in the background with logs.** While they run, keep working on independent tasks. After an interruption or a context reset, resume from the plan checkboxes, `native/DECISIONS.md`, and the logs. Never restart finished work.

## Ground rules

- **Git:** branch `native-shell` off `main`. Commit once per completed task: plain imperative subject (repo style, no `type(scope):`), ending with the session's attribution trailer. Never push, tag, release, or bump `apps/veil/package.json`.
- **Never write to the user's live Electron data** (`~/Library/Application Support/@pane/veil`, and `@pane/desktop` if present). Migration work reads a copy.
- **Chromium checkout:** lives at `~/chromium` (`~/chromium/depot_tools`, `~/chromium/src`), outside the repo. `~/chromium/src/pane` is a symlink to `native/engine/pane`.
- **Long builds:** run in the background under `nice`, with logs in `native/evidence/logs/`. Run one Chromium build at a time, and resume from the logs instead of restarting.
- **The existing gates stay green:** `bunx turbo run typecheck`, `bun run test`, `bun run lint`.
- **Evidence** goes under `native/evidence/phase-N/` (gitignored), with the file names given in each Acceptance list.
- **The Chromium checkout is the source of truth.** File and class names in this plan are starting points; verify each one in the checkout before relying on it.
- **Fixtures are local.** Every behavior check has an HTML fixture in `native/engine/tests/pages/`, served by the scenario runner's local server. Only CWS installs and the [HUMAN] checks touch the network.
- **Computer use with CUA is allowed.** Use the `cua-driver` skill and CLI whenever a check or step needs the real GUI. Load the skill before the first `cua-driver` command.
  - What it covers: launching and driving the Pane app and the stock Chromium test build, clicking native dialogs, popovers, and the toolbar, reading the accessibility tree, taking window screenshots, and opening Xcode, Finder (the dmg), or System Settings when a step needs them.
  - Prefer CDP and the engine trace for page state, and use CUA for what only the native UI shows.
  - Operate only the apps the task needs; never touch the user's other apps, accounts, or personal data.
  - If `cua-driver permissions status` shows a missing macOS permission, record it in `HUMAN-CHECKLIST.md` and fall back to CDP, the engine trace, and `CGWindowList` checks.

## Target layout

```
native/
  NOTICE                      third-party patch origins and licenses
  engine/
    CHROMIUM_VERSION          pinned stable tag
    args/release.gn           gn args
    patches/                  series, MANIFEST, NNNN-*.patch
    pane/                     symlinked into the checkout as //pane
      BUILD.gn
      public/                 Objective-C headers exported by PaneCore.framework
      browser/  browser/ui/  renderer/  common/
    scripts/                  sync.sh, apply-patches.sh, build.sh, package.sh, new-patch.sh
    tests/
      run.ts                  scenario runner (Bun): CDP client, local fixture server, trace checks
      scenarios/  pages/  tools/windows.swift
    UPGRADING.md
  app/
    project.yml               XcodeGen
    Pane/                     app target (AppKit + SwiftUI)
    PaneKit/                  Swift package: pure domain logic + tests
  evidence/                   gitignored
docs/adr/0004-native-shell-on-own-chromium-framework.md
docs/native/parity.md
```

---

## Phase 0 — Foundations

- [x] **0.1 [HUMAN]** Install full Xcode (only the Command Line Tools are present today), accept its license, and point `xcode-select` at it. Verify with `xcodebuild -version`.
- [x] **0.2** Write `docs/adr/0004-native-shell-on-own-chromium-framework.md`.
  - It records Decisions 1–7, the reasons (from this plan's references), and the native app's data root.
  - The data root must not collide with any directory the Electron app uses: check `@pane/veil`, `@pane/desktop`, and `Pane`.
  - Mark ADR 0003 as superseded by 0004.
- [x] **0.3** Create the `native/` layout.
  - Add `native/evidence/` and the generated `*.xcodeproj` to `.gitignore`.
  - Add a `native/README.md` that says how to sync, build, and run.
- [x] **0.4** Set up the checkout.
  - Install depot_tools in `~/chromium/depot_tools`.
  - Pin the newest Chrome stable for Mac in `native/engine/CHROMIUM_VERSION`. Prefer the `156.0.8078.x` line while it is current, because Ardia's patches target `156.0.8078.47`.
  - `scripts/sync.sh` fetches with `--no-history`, checks out the tag, runs `gclient sync`, and creates the `//pane` symlink. It must be idempotent.
- [x] **0.5** Write `args/release.gn`: `is_debug=false`, `dcheck_always_on=false`, `symbol_level=1`, `target_cpu="arm64"`, `proprietary_codecs=true`, `ffmpeg_branding="Chrome"`, `enable_nacl=false`, `is_component_build=false`. Then `scripts/build.sh` builds the stock `chrome` target.
- [x] **0.6** Write the scenario runner skeleton (`tests/run.ts`) and `tools/windows.swift`.
  - The runner launches a binary with `--remote-debugging-port`, serves fixtures, drives pages over CDP, and writes a JSON report.
  - `windows.swift` counts the on-screen windows owned by a PID via `CGWindowListCopyWindowInfo`.
- [x] **0.7** Check that CUA is ready: `cua-driver status` and `cua-driver permissions status`. Accessibility and Screen Recording must be granted; a missing grant goes to `HUMAN-CHECKLIST.md` (see the Autonomy policy).

**Acceptance**
- `xcodebuild -version` reports an Xcode whose SDK satisfies `build/config/mac/mac_sdk.gni` → `xcode.txt`
- `out/Release/Chromium.app/Contents/MacOS/Chromium --version` contains `CHROMIUM_VERSION` → `chromium-version.txt`
- A second run of `sync.sh` exits 0 without changing the checkout → `sync-rerun.log`
- The runner opens `pages/smoke.html` in stock Chromium and reads its title → `scenario-smoke.json`
- ADR 0004 exists, and ADR 0003 is marked superseded
- `cua-driver` reports itself ready, with Accessibility and Screen Recording granted → `cua-status.txt`
- The repo gates pass → `repo-gates.txt`

## Phase 1 — `PaneCore.framework` in host mode

- [x] **1.1** Patch: framework layout and branding.
  - Produce `PaneCore.framework` (bundle id `com.pane.desktop.framework`) with helpers `Pane Helper (Renderer|GPU|Plugin|Alerts).app`, exporting `ChromeMain`.
  - Starting points: the `chrome_framework` and helper targets in `chrome/BUILD.gn`, and `chrome/app/theme/chromium/BRANDING`.
- [x] **1.2** Patch: host mode.
  - When the Pane app launches the engine, Chrome creates no windows, menus, Dock behavior, first-run UI, default-browser prompt, or session restore of its own.
  - It is gated so stock Chromium behaves as before.
  - Starting points: `chrome/browser/app_controller_mac.mm`, `chrome/browser/chrome_browser_main_mac.mm`, `chrome/browser/ui/startup/startup_browser_creator.cc`.
- [x] **1.3** Write API v0 in `//pane/public`.
  - `PaneBrowserApplication`: start with a delegate, get a ready callback.
  - `PaneBrowserContext`: open a profile by id at `<data root>/Engine/<id>` (Chrome only accepts profile dirs directly under the user data dir).
  - `PaneWebContents`: create it in a context, call `loadURL`, get `nativeView`, receive delegate callbacks for title, URL, loading progress, and favicon.
- [x] **1.4** Add the engine trace.
  - When `PANE_ENGINE_TRACE=<file>` is set, the engine writes one JSONL line per API call and per delegate callback: name, arguments, result.
  - Every later scenario asserts against this trace.
- [x] **1.5** Build the Swift app.
  - XcodeGen project; `NSPrincipalClass` set to the NSApplication subclass Chromium requires (verify in the checkout); `main.swift` calls `ChromeMain`.
  - One `NSWindow`: a SwiftUI sidebar placeholder plus a content view hosting one `PaneWebContents`.
- [x] **1.6** Write `scripts/package.sh`. It embeds the framework in `Pane.app` and ad-hoc signs the nested code inside-out.

**Acceptance**
- The engine build, app build, and package all exit 0 → `build-engine.log`, `build-app.log`, `package.log`
- `Pane.app` launched with `--remote-debugging-port=9222` and `https://example.com` lists a page target with url `https://example.com/` and title `Example Domain` → `targets.json`
- Exactly one on-screen window is owned by the Pane process → `windows.json`
- The trace contains the create, `loadURL`, and title-change ("Example Domain") entries → `trace.jsonl`
- A `cua-driver` window screenshot shows the page inside the Pane window, and its accessibility read finds the sidebar placeholder and the web content area → `window.png`, `window-ax.txt`
- Stock Chromium built from the same checkout still opens its normal window, which proves host mode is gated → `stock-windows.json`
- Cmd+Q leaves no `Pane Helper` processes after 5 s → `quit.txt`

## Phase 2 — Browser core

- [x] **2.1** Write the parity matrix `docs/native/parity.md`, derived from `apps/veil`. Columns: feature · Electron source · native phase · status · evidence path. It must include:
  - **every tRPC procedure:**
    - `cws`: install, uninstall, installed
    - `extensions`: list
    - `hotkeys`: events
    - `profiles`: testProxy, load, unload, remove, signOut, signedIn
    - `security`: verify, setPin, changePin, removePin, wipe
    - `settings`: detectBrowser
    - `tabs`: open, close, switch, navigate, goBack, goForward, reload, stop, hideAll, showActive, find, stopFind, findResults
    - `ui`: present, dismiss, menu, confirm
    - `updates`: currentVersion, check, download
  - **every renderer surface:**
    - the browser page with its toolbar and find bar
    - the sidebar with profile and tab items, drag reordering, and the update pill
    - the Ctrl+Tab switcher
    - settings, including extension settings
    - the profile sheet
    - the lock screen with its numpad
    - the color picker
  - every hotkey in `hotkey-emitter.ts`;
  - every setting (`theme`, `tabSleepAfterMinutes`, `profileUnloadAfterMinutes`, `cacheBudgetMB`, `chromiumPath`);
  - every persisted store.
- [x] **2.2** Write the `PaneKit` package.
  - Domain models mirror `profile-store.ts`: `BrowserProfile`, `Tab`, `Fingerprint`, `ProxyConfig`, colors, group, keep loaded.
  - It persists JSON under the data root.
  - It holds the pure logic from `mru.ts`, `tab-switcher-grouping.ts`, and `compare-versions.ts`, with their vitest cases ported to Swift Testing.
- [x] **2.3** Engine API:
  - several contexts at once, with tabs per context;
  - navigation (back, forward, reload, stop, `canGoBack`/`canGoForward`);
  - new-window and popup requests sent to the delegate;
  - find in page with result counts;
  - downloads (destination, progress, completion);
  - favicon and loading progress;
  - renderer crash callback.
- [x] **2.4** Add the hidden Chrome `Browser` for each (window, profile) pair.
  - It is backed by Pane's implementation of the `BrowserWindow` interface, with no Views UI.
  - Starting points: `chrome/browser/ui/browser_window.h`, `Browser::CreateParams` in `browser.h`.
  - Evaluate Ardia's `chromium-ardia-layer` and `chromium-hosted-browsers-no-session-service`.
- [x] **2.5** Build the Swift UI.
  - A main window with the sidebar (profiles with color and tabs; drag to reorder profiles and tabs), the content area, and a toolbar (back, forward, reload, address).
  - The find bar, the Ctrl+Tab switcher grouped by profile, and the hotkeys.
  - It follows `DESIGN.md`: selection is instant, hover and enter/exit animate, `prefers-reduced-motion` is honored.
  - The theme drives `NSApp.appearance` and web content.

**Acceptance**
- `parity.md` lists every item named in 2.1 → `parity-check.txt` (grep of each procedure and surface name)
- `swift test` passes, and it includes every case ported from the three vitest files → `swift-test.txt`
- Scenario `profiles-isolation`: tabs of two profiles share one window, and a cookie set on a fixture in profile A is absent in profile B → `profiles-isolation.json`
- Scenarios `navigation`, `find`, `downloads`, `popup-new-tab`, and `crash-recovery` pass → `report.json`
- A relaunch restores profiles and tabs in order → `restore-before.json`, `restore-after.json`
- The Ctrl+Tab switcher shows groups per profile → `switcher.png`
- Each theme value (`system`, `light`, `dark`) sets the fixture's `matchMedia('(prefers-color-scheme: dark)')` as expected → `theme.json`

## Phase 3 — Chrome UI hooks

- [x] **3.1** Route every Chrome-owned UI surface to the app delegate, and implement each one natively:
  - JavaScript dialogs: alert, confirm, prompt, beforeunload (`components/javascript_dialogs`).
  - Permission prompts, keeping today's policy: `clipboard-sanitized-write` is allowed, everything else is prompted or denied as in `profile.ts` (`components/permissions`).
  - HTTP and proxy authentication (`LoginHandler` / `LoginTabHelper`).
  - The context menu, porting `packages/electron-chrome-context-menu`: cut, copy, paste, undo, redo, select all; spelling suggestions and add to dictionary; copy image; open link in new tab; inspect element (`RenderViewContextMenu`).
  - File choosers via `NSOpenPanel`/`NSSavePanel` (`FileSelectHelper`).
  - External protocols, hung renderer, sad tab, certificate interstitials, HTML5 fullscreen inside the window, picture-in-picture, print.
  - DevTools: open, close, inspect element. The sleep logic exempts tabs with DevTools open, so it needs that signal.
  - Autofill and password bubbles: Chrome's password manager and autofill prompts are disabled by default (Pane relies on extension password managers). Record that decision in `parity.md`.
- [x] **3.2** Evaluate and adapt Ardia's hook patches: `chromium-password-bubble-hook`, `chromium-autofill-address-bubble-window`, `chromium-device-chooser-hook`, `chromium-cast-dialog-hook`, `chromium-side-panel-hook`, `chromium-devtools-*`.

**Acceptance**
- There is one fixture and one scenario per surface, and all pass. For each, the trace shows the delegate callback, the native dialog is answered through `cua-driver` (by clicking its real button), and that answer reaches the page (for example, `confirm()` returns the chosen value) → `report.json`
- The window count before and after every scenario shows that no Chrome window or bubble ever appeared → `windows-per-scenario.json`
- Each Chrome UI factory named in 3.1 is either overridden in `//pane` or recorded as disabled in `parity.md` → `hooks-check.txt`

## Phase 4 — Extensions

- [x] **4.1** Run the extension system per profile.
  - The Chrome Web Store install flow works inside Pane tabs.
  - The install prompt is native (hook `ExtensionInstallPrompt`; evaluate Ardia's `chromium-extension-install-prompt-hook`).
- [x] **4.2** Make `chrome.tabs` and `chrome.windows` resolve to Pane tabs and windows.
  - "Current window" works from popups and from service workers (evaluate `chromium-hidden-window-current-window` and `chromium-extension-window-hidden`).
  - `tabs.create` opens a Pane tab in the same profile.
  - `windows.create` follows the decision recorded in `parity.md`.
- [x] **4.3** Build the toolbar actions API and its native toolbar.
  - Each action exposes icon, title, badge text and color, enabled state, and popup URL.
  - Popups are hosted in an `NSPopover` sized to their content.
  - Clicking an action without a popup fires `onClicked`.
- [x] **4.4** Build per-profile extension management: list, enable, disable, remove, pin, update. It replaces `extension-settings.tsx` and the `cws`/`extensions` routers.

**Acceptance** (the tested set in `docs/chrome-extension-support.md`; install clicks, toolbar clicks, and popup screenshots go through `cua-driver`)
- Dark Reader, installed from the CWS in profile A only, darkens a fixture's computed background in A but not in B, and its popup opens → `dark-reader.json`, `dark-reader-popup.png`
- uBlock Origin Lite, installed from the CWS, blocks a fixture's request to a filtered URL (CDP network log) → `ubol.json`
- The 1Password and NordPass popups render after install → `1password.png`, `nordpass.png`. **[HUMAN]** (deferred to the checklist): sign in inside each popup and confirm autofill on a fixture login form
- `chrome.tabs.query({active: true, currentWindow: true})` returns the active Pane tab from both a popup and a service worker, and `chrome.tabs.create` opens a Pane tab in the same profile → `tabs-api.json`
- No Chrome window appears in any extension scenario → `windows-per-scenario.json`

## Phase 5 — Pane features

- [x] **5.1** Per-profile proxy.
  - HTTP, HTTPS, SOCKS4, and SOCKS5 through each profile's proxy pref.
  - Proxy credentials go through the Phase 3 auth hook.
  - The SOCKS5-with-auth relay from `proxy-relay.ts` is ported (Chromium has no SOCKS5 auth).
  - The connection test from `proxy-test.ts` and the proxy status (`proxy-status-store.ts`).
  - The per-profile WebRTC IP handling policy.
- [x] **5.2** Per-profile fingerprint inside the engine.
  - It covers every `Fingerprint` field: userAgent, platform, screen, language(s), timezone, WebGL vendor and renderer, hardwareConcurrency, deviceMemory, maxTouchPoints, canvas noise, audio noise.
  - It includes the Client Hints derivation from `client-hints.ts`, with its tests ported to `PaneKit`.
  - It applies before page scripts in main frames, in iframes, in dedicated, shared, and service workers. Patched functions report native `toString`.
  - The config reaches each renderer process through its profile, since a renderer never serves two profiles.
  - Prefer Blink-level patches over script injection wherever the surface allows (UA/Client Hints, timezone, hardwareConcurrency, deviceMemory, screen). Record each borrowed patch in `native/NOTICE`.
- [x] **5.3** Google sign-in.
  - Automated check: `accounts.google.com` loads its identifier page in a profile without redirecting to `/rejected`, with and without a fingerprint.
  - Dropped by the user's decision (2026-10-26): the `/rejected` fallback (`google/auth-window.ts`, `google/sign-in.ts`: a real Chrome plus a cookie transfer) and Sign out of Google (`sign-out.ts`, `sessions.ts`). Both were workarounds for Google rejecting the Electron engine; a browser has neither, and Google accepts the native engine.
- [x] **5.4** Sleep, Unload, Keep loaded, and Trim, exactly as `CONTEXT.md` defines them.
  - Port the policy in `sleep-scheduler.ts` and all of `sleep-scheduler.test.ts` to `PaneKit`.
  - The engine exposes: discard (release the page, keep the tab entry), freeze, the audible/DevTools/recent-interaction signals, cache size, and trim. Trim clears the HTTP cache and service worker caches only, never cookies, local storage, or IndexedDB.
- [x] **5.5** Security.
  - The lock screen with the PIN numpad, plus set, change, and remove PIN.
  - Existing bcrypt hashes from `security.json` are verified with a vetted bcrypt implementation; no homemade crypto.
  - The panic wipe as in `security/wipe.ts`.
- [x] **5.6** The remaining surfaces.
  - Settings: theme, sleep and unload minutes, cache budget.
  - The profile sheet: name, color, group, the fingerprint editor with `default-fingerprints.ts` presets and `host-webgl.ts` detection, the proxy editor with its test, keep loaded; a fingerprint change reopens the profile's open tabs in new renderers (5.2 applies it to new renderers only).
  - The update check through the GitHub Releases API (ADR 0002), with the quiet sidebar pill from `DESIGN.md`.
  - Chrome's own background requests to Google (seen in 5.1: `accounts.google.com`, `www.google.com`): find their sources, and turn off those the Electron app never made.
- [x] **5.7** Migration from the Electron app on first launch, always from a copy of the Electron data directory, never in place.
  - It imports `profiles.json`, `settings.json`, `security.json`, and each `Partitions/profile-<id>`.
  - Cookies must stay decryptable: match the os_crypt Keychain service name (see Ardia's `chromium-safe-storage-name`) or re-encrypt them.
  - It reinstalls or migrates each profile's extensions.

**Acceptance**
- Against local test proxies started by the runner (HTTP with auth, SOCKS5 with auth), profile A's requests go through A's proxy (proxy logs) while profile B connects directly, and the WebRTC fixture shows no local IP leak in A → `proxy.json`
- For two profiles with different configs, the fingerprint fixture reports exactly each profile's configured values in a main frame, an iframe, a dedicated worker, and a service worker. `toString` checks pass, and the `Sec-CH-UA*` headers a local echo server receives match `navigator.userAgentData` → `fingerprint.json`
- The Google identifier page loads with no `/rejected` redirect, with and without a fingerprint → `google.json`. **[HUMAN]** (deferred to the checklist): sign in to a real Google account
- `PaneKit` tests include every case ported from `sleep-scheduler.test.ts` and `client-hints.test.ts`, and all pass → `swift-test.txt`
- Scenario `sleep` (debug time scale): a tab sleeps after the configured minutes, a Keep-loaded profile never sleeps, and a trimmed profile keeps its login cookie → `sleep.json`
- Scenario `migration` on a copied data directory: profiles, tabs, settings, and PIN migrate, and a cookie set in the Electron app exists in the native profile → `migration.json`
- Every `parity.md` row is ✅ with an evidence path, or marked dropped with its technical justification and a matching `HUMAN-CHECKLIST.md` entry

## Phase 6 — Distribution and maintenance

- [x] **6.1** Write the release packaging.
  - It produces `Pane.app` with `PaneCore.framework` inside, and a dmg named `Pane-<version>-arm64.dmg`, which keeps the in-app update check's contract.
  - The native app gets a single version source.
- [x] **6.2 [HUMAN]** (deferred) Apple Developer ID and a `notarytool` Keychain profile. Build the signing and notarization steps so they activate when an identity exists. Until then, builds are ad-hoc signed as today, and the checklist entry says how to enable them.
- [ ] **6.3** Prebuilt engine.
  - `package.sh` emits a versioned PaneCore zip with a checksum.
  - The app builds against a prebuilt engine without a Chromium checkout (Ardia's `prebuilt-engines.tsv` model).
- [ ] **6.4** Upgrade runbook.
  - Write `native/engine/UPGRADING.md`.
  - Perform one real upgrade to a newer stable tag: re-apply the patches, rebuild, and get every scenario green.
- [ ] **6.5** CI.
  - A GitHub workflow builds the app against the prebuilt engine and runs the `PaneKit` tests on macOS runners.
  - Validate it locally: lint the YAML, and run its build and test commands on this Mac.
  - Engine builds stay local or self-hosted. Provisioning a runner and the first real CI run (which needs a push) go to the checklist.
- [x] **6.6** Sparkle only after 6.2. Until then, keep the manual dmg flow from `DESIGN.md`.

**Acceptance**
- The dmg is produced and mounts, and the installed app passes the smoke scenario → `dist.log`, `smoke.json`
- `codesign --verify --deep --strict` passes on the ad-hoc signed app → `codesign.txt`. Notarization and `spctl` are deferred to the checklist with 6.2
- The upgrade log exists, and every scenario passes on the new tag → `upgrade.log`, `report.json`
- The workflow YAML passes lint, and its build and test commands pass locally → `ci-local.txt`. The first real CI run is on the checklist

## Phase 7 — Cutover (on the `native-shell` branch)

- [ ] **7.1** Write `docs/native/cutover-report.md`: the parity summary, every dropped item with its justification, and every open `HUMAN-CHECKLIST.md` entry.
- [ ] **7.2** On the branch:
  - Remove `apps/veil` and the Electron-only packages (`electron-chrome-extensions`, `electron-chrome-context-menu`, and `ui` if unused).
  - Update `AGENTS.md`, `README`, and `.github/workflows/release.yml` for the native app.
  - `main` keeps the Electron app until the user merges.
- [ ] **7.3** Prepare the first native release under the `AGENTS.md` version-bump rule (a major bump fits): the version bump, a local dmg build, and the exact commands to merge, tag, and push, written in `HUMAN-CHECKLIST.md`. Do not publish.

**Acceptance**
- The remaining repo gates pass, and the release workflow passes a local dry run of its build steps → `cutover.txt`
- `cutover-report.md` exists, and `HUMAN-CHECKLIST.md` ends with the publish steps

## Risks to watch

- **Host-mode and hook patches break on Chromium upgrades.** Keep them small; the scenario suite is the safety net.
- **Unhooked Chrome UI shows up as Chrome windows or bubbles.** The per-scenario window count catches it.
- **Chromium owns the run loop.** Swift work on the main actor must never block it or re-enter it. Atrium hit reentrancy panics doing exactly that.
- **Shipping a browser means shipping Chromium security fixes,** with a new stable release almost every week. Phase 6 must make an upgrade a routine, not a project.
- **Widevine is unavailable** (it requires a Google license), the same as today.
