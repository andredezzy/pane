# Native parity matrix

Every user-facing capability of the Electron app (`apps/veil`) and where the native app delivers it. Phase 5 is complete only when every row is ✅ with an evidence path, or ⛔ with a technical justification and a matching `native/HUMAN-CHECKLIST.md` entry. Evidence paths are relative to `native/evidence/`.

Status: ✅ done and proven · 🟡 in progress · — not started · ⛔ dropped (justified)

## tRPC procedures (`apps/veil/src/main/trpc/routers/`)

| Feature | Electron source | Phase | Status | Evidence |
|---|---|---|---|---|
| `cws.install` — install an extension from the Chrome Web Store | `routers/cws.ts`, `main/extensions.ts` | 4 | ✅ | The Web Store page's own Add to Chrome, then Pane's install sheet (Chrome's `ExtensionInstallPrompt`, hooked): `phase-4/dark-reader.json`, `phase-4/ubol.json`, `phase-4/password-managers.json`; by ID in Settings: `phase-4/extension-management.json` |
| `cws.uninstall` — remove an extension | `routers/cws.ts` | 4 | ✅ | Settings › Extensions › Remove, after a confirmation: `phase-4/extension-management.json` |
| `cws.installed` — list installed Web Store extensions | `routers/cws.ts` | 4 | ✅ | `PaneBrowserContext.extensions`: `phase-4/extension-management.json` |
| `extensions.list` — extensions loaded in a profile | `routers/extensions.ts` | 4 | ✅ | Per profile, Dark Reader in A only: `phase-4/dark-reader.json` |
| `hotkeys.events` — app shortcuts reach the UI | `routers/hotkeys.ts`, `emitters/hotkey-emitter.ts` | 2 | ✅ | `phase-2/shortcuts.json` — the app's own menu and key monitor; no IPC |
| `profiles.testProxy` — test a proxy before saving | `routers/profiles.ts`, `profile/proxy-test.ts` | 5 | ✅ | `ProxyTest`: the same IP echo through the proxy, SOCKS through the relay: `phase-5/proxy.json` (status connected for HTTP with auth and SOCKS5 with auth) |
| `profiles.load` — load a profile's session | `routers/profiles.ts`, `pane.ts` | 2 | ✅ | `phase-2/profiles-isolation.json`, `phase-2/profile-lifecycle.json` |
| `profiles.unload` — release a hidden profile (Unload) | `routers/profiles.ts`, `sleep-scheduler.ts` | 5 | ✅ | Collapsing a profile unloads it, as in the Electron app, and so does Collapse all: `phase-2/profile-lifecycle.json`; the scheduler's automatic Unload: `phase-5/sleep.json` |
| `profiles.remove` — delete a profile and its data | `routers/profiles.ts` | 2 | ✅ | `phase-2/profile-lifecycle.json` — profile, tabs, pages and engine directory gone |
| `profiles.signOut` — sign out of Google in a profile | `routers/profiles.ts`, `profile/google/sign-out.ts` | 5 | dropped | The user's decision (2026-10-26): a workaround for Google rejecting the Electron engine, which no browser has; Google accepts the native engine (`phase-5/google.json`). `HUMAN-CHECKLIST.md` (Done) |
| `profiles.signedIn` — whether a profile has a Google session | `routers/profiles.ts`, `profile/google/sessions.ts` | 5 | dropped | Only fed Sign out of Google, dropped with it (the user's decision, 2026-10-26) |
| `security.verify` — check the PIN | `routers/security.ts` | 5 | ✅ | OpenBSD's bcrypt verifies the Electron app's bcryptjs hash; a wrong PIN counts on disk at once: `phase-5/security.json`, `phase-5/swift-test.txt` |
| `security.setPin` / `changePin` / `removePin` | `routers/security.ts`, `stores/security-store.ts` | 5 | ✅ | Through the PIN screen, `$2b$` at cost 10 as the Electron app: `phase-5/security.json` |
| `security.wipe` — panic wipe | `routers/security.ts`, `security/wipe.ts` | 5 | ✅ | Only after the fifth wrong PIN at the lock: the engine data (every profile) and caches, and the app's files, overwritten three times then deleted; Pane starts again fresh: `phase-5/security.json` |
| `settings.detectBrowser` — find Chrome for the Google sign-in fallback | `routers/settings.ts`, `detect-browser.ts` | 5 | dropped | Only served the Google sign-in fallback, dropped (the user's decision, 2026-10-26) |
| `stores.push` / `stores.sync` — state sync between processes | `routers/stores.ts`, `stores/middlewares/sync.ts` | — | ⛔ | Not a capability: the native app is one process with one state, so nothing syncs. |
| `tabs.open` / `close` / `switch` | `routers/tabs.ts`, `profile/profile-tabs.ts` | 2 | ✅ | `phase-2/shortcuts.json`, `phase-2/popup-new-tab.json`, `phase-2/switcher.json` |
| `tabs.navigate` / `goBack` / `goForward` / `reload` / `stop` | `routers/tabs.ts` | 2 | ✅ | `phase-2/navigation.json`, `phase-2/shortcuts.json` |
| `tabs.hideAll` / `showActive` — hide pages while an overlay is up | `routers/tabs.ts` | — | ⛔ | Electron-only: its overlays could not draw above page views. Native sheets and popovers draw above pages. |
| `tabs.find` / `stopFind` / `findResults` — find in page | `routers/tabs.ts`, `emitters/find-emitter.ts` | 2 | ✅ | `phase-2/find.json`, `phase-2/shortcuts.json` |
| `ui.present` / `dismiss` — overlay surface for sheets | `routers/ui.ts`, `renderer/app/surface.tsx` | — | ⛔ | Electron-only overlay window; replaced by native sheets/popovers (each listed below as its own surface). |
| `ui.menu` — native context menus (profile, tab) | `routers/ui.ts`, `renderer/app/layout.tsx` | 2 | 🟡 | Built (SwiftUI context menus on profile and tab rows); on-screen check waits for an unlocked screen |
| `ui.confirm` — confirmation dialogs | `routers/ui.ts` | 2 | 🟡 | Built (delete-profile alert); on-screen check waits for an unlocked screen |
| `updates.currentVersion` / `check` / `download` | `routers/updates.ts`, `updater.ts` | 5 | ✅ | The latest GitHub Release at launch and daily, retries after a failure; the arm64 dmg downloaded to Downloads with progress, then opened: `phase-5/updates.json` (against a stand-in release; nothing reaches GitHub in scenarios) |

## Screens and surfaces (`apps/veil/src/renderer/`)

| Feature | Electron source | Phase | Status | Evidence |
|---|---|---|---|---|
| Hosted engine: the app's window shows a page | (new) | 1 | 🟡 | `phase-1/`, `phase-2/hosted.json` — all but the visual check, which waits for an unlocked screen |
| Sidebar: profiles (name, color, group) with their tabs | `components/sidebar/sidebar.tsx`, `profile-item.tsx` | 2 | ✅ | `phase-2/switcher.png`, `phase-2/restore-after.json` |
| Sidebar: profile expand/collapse (collapsed on launch) | `stores/sidebar-store.ts` | 2 | ✅ | `phase-2/restore.json` (collapsed on launch), `phase-2/profile-lifecycle.json` (collapse unloads) |
| Sidebar: tab rows (favicon, title, loading, sleeping dimmed) | `components/sidebar/tab-item.tsx` | 2 | 🟡 | Title, favicon, loading and dimmed state are in the model (`phase-2/navigation.json`, `phase-2/profile-lifecycle.json`); the rendered row waits for an unlocked screen |
| Sidebar: drag to reorder profiles and tabs | `docs/superpowers/plans/2026-05-06-sidebar-reordering.md` | 2 | 🟡 | Reorder model proven (`phase-2/restore.json`); the drag gesture waits for an unlocked screen |
| Profile context menu: Edit, Keep loaded, Delete (Sign out of Google dropped) | `app/layout.tsx` | 2 | 🟡 | Built; the on-screen menu check waits for an unlocked, idle screen. Sign out of Google: dropped by the user's decision (see its row) |
| Tab context menu | `components/sidebar/tab-item.tsx` | 2 | 🟡 | Built; on-screen check waits for an unlocked screen |
| Toolbar: back, forward, reload/stop, address field, extension actions | `app/browser/_components/toolbar.tsx`, `browser-action-list.tsx` | 2 (4 for actions) | ✅ | Back, forward, reload/stop, address (following the selected tab): `phase-2/navigation.json`, `phase-2/shortcuts.json`, `phase-2/switcher.png`, `phase-4/regression/navigation.json`; pinned actions with icon and badge, the extensions menu, popups in a popover sized to the page, clicked with CUA: `phase-4/dark-reader.json`, `phase-4/dark-reader-popup.png`, `phase-4/password-managers.json` |
| Page framed in a rounded, inset card with a soft shadow | `components/content-panel.tsx` | 2 | 🟡 | Card visible in `phase-2/switcher.png`; page pixels inside it wait for an unlocked screen |
| Empty state (no profile or tab) | `app/browser/_components/empty-state.tsx` | 2 | ✅ | `phase-2/empty-state.png`, `phase-2/profile-lifecycle.json` |
| Find bar (count, next, previous, Escape) | `app/browser/_components/find-bar.tsx` | 2 | ✅ | `phase-2/find.json`, `phase-2/shortcuts.json` (⌘F focus, Escape) |
| Tab loading progress | `docs/superpowers/plans/` (tab-loading-progress) | 2 | ✅ | `phase-2/navigation.json` (slow load reports loading, stop ends it) |
| Ctrl+Tab switcher, MRU, grouped by profile | `components/tab-switcher.tsx`, `tab-switcher-grouping.ts` | 2 | ✅ | `phase-2/switcher.json`, `phase-2/switcher.png`, `phase-2/shortcuts.json` |
| Profile sheet: name, color, group | `sheets/profile-sheet.tsx`, `components/color-picker.tsx` | 2 | 🟡 | Built; on-screen check waits for an unlocked screen |
| Profile sheet: fingerprint (platform, presets, spoof toggle) | `sheets/profile-sheet.tsx`, `components/default-fingerprints.ts`, `host-webgl.ts` | 5 | ✅ | Filled with CUA: the Mac's platform claims the Mac's GPU (Chrome's GPU info, the very strings WebGL reports), a rename keeps the fingerprint, another platform gets its preset and the cross-OS note, spoofing off clears it; a change reaches the profile's open tabs: `phase-5/profile-sheet.json`, `phase-5/profile-sheet-cross-os.png` |
| Profile sheet: proxy (type, host, port, user, password, paste `host:port:user:pass`, test) | `sheets/profile-sheet.tsx` | 5 | ✅ | The paste shortcut fills the fields; the test answers through the proxy (its IP on the green button); the proxy saves: `phase-5/profile-sheet.json`, `phase-5/profile-sheet-proxy.png` |
| Profile sheet: Keep loaded | `sheets/profile-sheet.tsx` | 5 | ✅ | In the profile menu, where the Electron app has it (its sheet has no Keep loaded field); followed by the scheduler: `phase-5/sleep.json` |
| Settings: Appearance (theme) | `app/settings/page.tsx` | 2 | ✅ | `phase-2/theme.json`, `phase-2/shortcuts.json` (⌘,) |
| Settings: Browser (Chrome path, auto-detect) | `app/settings/page.tsx` | 5 | dropped | Only served the Google sign-in fallback, dropped by the user's decision (2026-10-26) |
| Settings: Resources (sleep after, unload after, trim cache over) | `app/settings/_components/resource-settings.tsx` | 5 | ✅ | Memory & disk with the Electron presets, after Appearance: `phase-5/update-settings.png`; followed by the scheduler: `phase-5/sleep.json` |
| Settings: Extensions (install from Web Store URL/ID, list, uninstall) | `app/settings/_components/extension-settings.tsx` | 4 | ✅ | Install by ID, disable, enable, pin, unpin, update, remove, each clicked with CUA: `phase-4/extension-management.json` |
| Settings: Security (set up, change, remove PIN) | `app/settings/page.tsx` | 5 | ✅ | Set up PIN, or Change and Remove PIN, after Extensions as in the Electron app; each opens the PIN screen: `phase-5/security.json` |
| Settings: Updates (check, download) | `app/settings/page.tsx`, `components/update-status-label.tsx` | 5 | ✅ | Check for updates and when it last checked, or the version and Download update with its progress and time left: `phase-5/updates.json`, `phase-5/update-settings.png` |
| Lock screen with PIN numpad | `app/lock-screen/page.tsx`, `components/numpad.tsx` | 5 | ✅ | The panel over the whole window, dots, numpad, hints, shake, slide in and out; keys typed go to it alone: `phase-5/security-lock.png`, `phase-5/security-last-attempt.png`. The update pill on it waits for the update check (5.6) |
| Update pill in the sidebar footer (dismissible per launch) | `components/sidebar/sidebar.tsx`, DESIGN.md | 5 | ✅ | Its click downloads, the dot pulses and the bar fills meanwhile, dismissing hides it for this version until relaunch; the lock screen shows the same status: `phase-5/updates.json`, `phase-5/update-pill.png` |

## Shortcuts (`apps/veil/src/main/window.ts`)

| Feature | Keys | Phase | Status | Evidence |
|---|---|---|---|---|
| New tab (focuses the address field) | ⌘T | 2 | ✅ | `phase-2/shortcuts.json` |
| Close tab | ⌘W | 2 | ✅ | `phase-2/shortcuts.json` |
| Reopen closed tab (last 20, across profiles) | ⇧⌘T | 2 | ✅ | `phase-2/shortcuts.json` |
| Find in page | ⌘F | 2 | ✅ | `phase-2/shortcuts.json` |
| Reload | ⌘R | 2 | ✅ | `phase-2/shortcuts.json` |
| Page DevTools | ⇧⌘I | 3 | ✅ | `phase-3/devtools.json` — docked beside the tab in the app's window; `phase-3/context-menu.json` (Inspect) |
| App DevTools | ⌥⌘I | — | ⛔ | Electron-only: the app UI is native, so there is no app web UI to inspect. |
| Zoom in / out / reset | ⌘+ / ⌘− / ⌘0 | 2 | ✅ | `phase-2/shortcuts.json` (engine zoom level; the page's own ratio re-checks on an unlocked screen) |
| Toggle full screen | ⌃⌘F | 2 | 🟡 | Built (View menu); full screen needs an unlocked screen |
| Back / Forward | ⌘[ / ⌘] | 2 | ✅ | `phase-2/shortcuts.json` |
| Focus address field | ⌘L | 2 | ✅ | `phase-2/shortcuts.json` |
| Tab switcher forward / backward (MRU) | ⌃⇥ / ⌃⇧⇥ | 2 | ✅ | `phase-2/shortcuts.json` |
| Go to tab 1–9 | ⌘1 … ⌘9 | 2 | ✅ | `phase-2/shortcuts.json` (⌘1, ⌘2) |
| Check for updates (app menu) | — | 5 | ✅ | Pane › Check for Updates…, chosen through accessibility: `phase-5/updates.json` |
| Edit menu (undo, redo, cut, copy, paste, select all) | standard | 2 | 🟡 | Built (standard responder actions); not exercised, to keep the user's clipboard untouched; on-screen check |

## Settings and persisted state (`apps/veil/src/stores/`)

| Feature | Electron source | Phase | Status | Evidence |
|---|---|---|---|---|
| `theme`: system / light / dark — chrome, menus, and `prefers-color-scheme` follow (ADR 0001) | `settings-store.ts` | 2 | ✅ | `phase-2/theme.json` |
| `tabSleepAfterMinutes` | `settings-store.ts` | 5 | ✅ | Settings › Memory & disk, followed by the scheduler: `phase-5/sleep.json` |
| `profileUnloadAfterMinutes` | `settings-store.ts` | 5 | ✅ | Settings › Memory & disk, followed by the scheduler: `phase-5/sleep.json` |
| `cacheBudgetMB` | `settings-store.ts` | 5 | ✅ | Settings › Memory & disk, followed by the scheduler: `phase-5/sleep.json` |
| `chromiumPath` (Google sign-in fallback) | `settings-store.ts` | 5 | dropped | Only served the Google sign-in fallback, dropped (the user's decision, 2026-10-26); the field still round-trips in `settings.json` |
| Profiles: id, name, color, group, fingerprint, proxy, keepLoaded, tabs (id, url, title, favicon, isLoaded), timestamps — persisted | `profile-store.ts` (`profiles.json`) | 2 (5 for fingerprint/proxy) | ✅ | `phase-2/restore-before.json`, `phase-2/restore-after.json`, `phase-2/swift-test.txt` (fingerprint/proxy: Phase 5) |
| Active tab and profile, MRU history, closed-tab stack | `tab-store.ts` | 2 | ✅ | `phase-2/shortcuts.json`, `phase-2/switcher.json`, `phase-2/swift-test.txt` |
| Expanded profiles (not persisted) | `sidebar-store.ts` | 2 | ✅ | `phase-2/restore.json` |
| Current page: browser or settings | `navigation-store.ts` | 2 | ✅ | `phase-2/shortcuts.json` (⌘,) |
| PIN hash (bcrypt) and lock state | `security-store.ts` (`security.json`) | 5 | ✅ | `security.json` reads and writes in the Electron app's format; a PIN means Pane starts locked: `phase-5/security.json`, `phase-5/swift-test.txt` |
| Installed extensions per profile | `extension-store.ts` | 4 | ✅ | Chrome's extension system, one per profile (the store is gone): `phase-4/dark-reader.json` |
| Proxy status per profile | `proxy-status-store.ts` | 5 | ✅ | Tested at launch and on every proxy change; the sidebar's dot: `phase-5/proxy.json` |
| Update status (checking, available, downloading) | `update-store.ts` | 5 | ✅ | One source for the pill, Settings, and the lock screen (`Updater`); not persisted, as in the Electron app: `phase-5/updates.json` |
| Migration of all of the above from the Electron app | (new) | 5 | ✅ | First launch, from a copy, never in place: profiles, tabs, settings, the PIN, each profile's site data (Electron's own cookie and local storage reach the profile's pages), extensions reinstalled from the Web Store as each profile opens; the source is byte for byte unchanged: `phase-5/migration.json`, `phase-5/swift-test.txt`. **[HUMAN]** the real Keychain approval: `HUMAN-CHECKLIST.md` |

## Chrome's own UI (Phase 3)

Surfaces Chrome would show in its own dialogs, bubbles, and windows. Each is the app's now, or off. `phase-3/hooks-check.txt` finds the code for every one; `phase-3/windows-per-scenario.json` shows no Chrome window or bubble in any scenario.

| Feature | Chrome source | Phase | Status | Evidence |
|---|---|---|---|---|
| JavaScript dialogs: alert, confirm, prompt | `components/javascript_dialogs` | 3 | ✅ | `phase-3/dialog-alert.json`, `dialog-confirm.json`, `dialog-prompt.json` |
| beforeunload ("Leave site?") | `components/javascript_dialogs` | 3 | ✅ | `phase-3/dialog-beforeunload.json` — Cancel keeps the tab |
| Form resubmission warning | `BrowserWebContentsDelegate::ShowRepostFormWarningDialog` | 3 | ✅ | `phase-3/dialog-form-resubmission.json` |
| Permission prompts | `components/permissions` | 3 | ✅ | `phase-3/permission-prompt.json` |
| HTTP authentication | `LoginHandler` | 3 | ✅ | `phase-3/http-auth.json` |
| Proxy authentication | `LoginHandler` | 3 | ✅ | `phase-3/proxy-auth.json` |
| File choosers (inputs, File System Access, Save As) | `ui::SelectFileDialog` | 3 | ✅ | `phase-3/file-chooser.json` |
| External protocols | `ExternalProtocolHandler` | 3 | ✅ | `phase-3/external-protocol.json` — `mailto:` opens directly, as in Chrome |
| Hung renderer | `HungRendererDialogView` | 3 | ✅ | `phase-3/hung-renderer.json` |
| Sad tab | `SadTabView` | 3 | ✅ | `phase-3/sad-tab.json` — a crash loop shows the app's sad tab |
| Certificate interstitials | `SSLBlockingPage` (web content) | 3 | ✅ | `phase-3/certificate-interstitial.json` |
| HTML5 fullscreen inside the window, Esc hint | `ExclusiveAccessBubbleViews` | 3 | ✅ | `phase-3/fullscreen.json` |
| Picture-in-picture | `VideoOverlayWindowViews` | 3 | ✅ | `phase-3/picture-in-picture.json` — Chromium's floating player kept by decision (DECISIONS.md) |
| Print | Print preview (WebUI) | 3 | ✅ | `phase-3/print.json` — the system print panel, ⌘P and window.print() |
| Device choosers (USB, HID, serial, Bluetooth) | `ChooserBubbleUi` | 3 | ✅ | `phase-3/device-chooser.json` — declined, as in the Electron app |
| Screen capture picker, Cast dialog | `DesktopMediaPickerViews`, `CastDialogView` | 3 | ✅ | `phase-3/screen-capture.json` — declined, as in the Electron app |
| Undocked DevTools window | `DevToolsWindow` (its own Browser) | 3 | ✅ | `phase-3/devtools-undocked.json` — a window of Pane's showing only DevTools |

## Engine behavior

| Feature | Electron source | Phase | Status | Evidence |
|---|---|---|---|---|
| Per-profile isolation (cookies, storage, cache) | `profile/profile.ts` (`persist:profile-<id>`) | 2 | ✅ | `phase-2/profiles-isolation.json` |
| Permission policy: clipboard writes allowed, the rest denied or prompted | `profile/profile.ts` | 3 | ✅ | `phase-3/permission-prompt.json` — clipboard-write granted without a prompt; other requests in the app's sheet |
| Spellcheck suggestions and add to dictionary | `packages/electron-chrome-context-menu` | 3 | 🟡 | Built (`phase-3/context-menu.json`); macOS flags no misspelling while the screen is locked (stock Chrome for Testing neither), so suggestions re-check on an unlocked screen. Add to Dictionary writes the system dictionary, so scenarios list it but do not click it |
| Page context menu (copy, paste, image, links, inspect) | `packages/electron-chrome-context-menu` | 3 | ✅ | `phase-3/context-menu.json` — the app's NSMenu, in the Electron app's order |
| Downloads | `profile/profile-tabs.ts` | 2 | ✅ | `phase-2/downloads.json` |
| Popups and `window.open` open as tabs | `profile/profile-tabs.ts` | 2 | ✅ | `phase-2/popup-new-tab.json` |
| Renderer crash recovery | (new) | 2 | ✅ | `phase-2/crash-recovery.json` |
| Favicons | `profile/favicon.ts` | 2 | ✅ | `phase-2/navigation.json` |
| Per-profile proxy, SOCKS5 with auth relay, WebRTC IP policy | `profile/proxy-relay.ts`, `profile.ts` | 5 | ✅ | Chrome's per-profile proxy pref and `webrtc.ip_handling_policy`; the relay in Swift (SOCKS4/5 upstream with credentials); credentials answer the proxy's challenge without a prompt: `phase-5/proxy.json` |
| Per-profile fingerprint and Client Hints | `profile/fingerprint-preload.ts`, `client-hints.ts` | 5 | ✅ | In Blink and the browser, not script (patch `0006-pane-fingerprint`): every `Fingerprint` field in a frame, an iframe, and dedicated, shared, and service workers, from the page's first script, as native functions; the User-Agent and Client Hints per profile on navigations, fetches, workers, and the network context, with Chromium's own GREASE brand: `phase-5/fingerprint.json`; the derivation and `client-hints.test.ts` in PaneKit: `phase-5/swift-test.txt` |
| Google sign-in fallback (real Chrome + cookie import) | `profile/google/*` | 5 | dropped | The user's decision (2026-10-26): a workaround for Google rejecting the Electron engine, which no browser has. Google accepts the native engine, with and without a fingerprint: `phase-5/google.json`. `HUMAN-CHECKLIST.md` (Done) |
| Sleep, Unload, Keep loaded, Trim | `sleep-scheduler.ts`, CONTEXT.md | 5 | ✅ | The scheduler in PaneKit with every case of `sleep-scheduler.test.ts` (`phase-5/swift-test.txt`); a tab sleeps on time, the selected tab, one with DevTools open, and a Keep loaded profile stay, a hidden profile unloads and is trimmed (HTTP cache, Cache Storage, service workers) and stays signed in with its local storage: `phase-5/sleep.json` |
| Chrome extensions (actions, popups, tabs/windows APIs) | `packages/electron-chrome-extensions` | 4 | ✅ | Chrome's own, in hidden Browsers: `tabs.query({active, currentWindow})` from a popup and a worker, `tabs.create`, `tabs.update({active})`: `phase-4/tabs-api.json`; `windows.create` opens a tab in the extension's profile, as the Electron app did (Pane has one window per profile set, no popup windows): `phase-4/tabs-api.json`; blocking rules (uBlock Origin Lite): `phase-4/ubol.json`; password managers: `phase-4/password-managers.json` |
| Chrome password manager and autofill prompts | — | 3 | ✅ | Off by default (Pane relies on extension password managers): `phase-3/password-manager-off.json`, `phase-3/hooks-check.txt` |
