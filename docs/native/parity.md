# Native parity matrix

Every user-facing capability of the Electron app (`apps/veil`) and where the native app delivers it. Phase 5 is complete only when every row is ✅ with an evidence path, or ⛔ with a technical justification and a matching `native/HUMAN-CHECKLIST.md` entry. Evidence paths are relative to `native/evidence/`.

Status: ✅ done and proven · 🟡 in progress · — not started · ⛔ dropped (justified)

## tRPC procedures (`apps/veil/src/main/trpc/routers/`)

| Feature | Electron source | Phase | Status | Evidence |
|---|---|---|---|---|
| `cws.install` — install an extension from the Chrome Web Store | `routers/cws.ts`, `main/extensions.ts` | 4 | — | |
| `cws.uninstall` — remove an extension | `routers/cws.ts` | 4 | — | |
| `cws.installed` — list installed Web Store extensions | `routers/cws.ts` | 4 | — | |
| `extensions.list` — extensions loaded in a profile | `routers/extensions.ts` | 4 | — | |
| `hotkeys.events` — app shortcuts reach the UI | `routers/hotkeys.ts`, `emitters/hotkey-emitter.ts` | 2 | — | |
| `profiles.testProxy` — test a proxy before saving | `routers/profiles.ts`, `profile/proxy-test.ts` | 5 | — | |
| `profiles.load` — load a profile's session | `routers/profiles.ts`, `pane.ts` | 2 | — | |
| `profiles.unload` — release a hidden profile (Unload) | `routers/profiles.ts`, `sleep-scheduler.ts` | 5 | — | |
| `profiles.remove` — delete a profile and its data | `routers/profiles.ts` | 2 | — | |
| `profiles.signOut` — sign out of Google in a profile | `routers/profiles.ts`, `profile/google/sign-out.ts` | 5 | — | |
| `profiles.signedIn` — whether a profile has a Google session | `routers/profiles.ts`, `profile/google/sessions.ts` | 5 | — | |
| `security.verify` — check the PIN | `routers/security.ts` | 5 | — | |
| `security.setPin` / `changePin` / `removePin` | `routers/security.ts`, `stores/security-store.ts` | 5 | — | |
| `security.wipe` — panic wipe | `routers/security.ts`, `security/wipe.ts` | 5 | — | |
| `settings.detectBrowser` — find Chrome for the Google sign-in fallback | `routers/settings.ts`, `detect-browser.ts` | 5 | — | |
| `stores.push` / `stores.sync` — state sync between processes | `routers/stores.ts`, `stores/middlewares/sync.ts` | — | ⛔ | Not a capability: the native app is one process with one state, so nothing syncs. |
| `tabs.open` / `close` / `switch` | `routers/tabs.ts`, `profile/profile-tabs.ts` | 2 | — | |
| `tabs.navigate` / `goBack` / `goForward` / `reload` / `stop` | `routers/tabs.ts` | 2 | — | |
| `tabs.hideAll` / `showActive` — hide pages while an overlay is up | `routers/tabs.ts` | — | ⛔ | Electron-only: its overlays could not draw above page views. Native sheets and popovers draw above pages. |
| `tabs.find` / `stopFind` / `findResults` — find in page | `routers/tabs.ts`, `emitters/find-emitter.ts` | 2 | — | |
| `ui.present` / `dismiss` — overlay surface for sheets | `routers/ui.ts`, `renderer/app/surface.tsx` | — | ⛔ | Electron-only overlay window; replaced by native sheets/popovers (each listed below as its own surface). |
| `ui.menu` — native context menus (profile, tab) | `routers/ui.ts`, `renderer/app/layout.tsx` | 2 | — | |
| `ui.confirm` — confirmation dialogs | `routers/ui.ts` | 2 | — | |
| `updates.currentVersion` / `check` / `download` | `routers/updates.ts`, `updater.ts` | 5 | — | |

## Screens and surfaces (`apps/veil/src/renderer/`)

| Feature | Electron source | Phase | Status | Evidence |
|---|---|---|---|---|
| Hosted engine: the app's window shows a page | (new) | 1 | 🟡 | `phase-1/` — all but the visual check, which waits for an unlocked screen |
| Sidebar: profiles (name, color, group) with their tabs | `components/sidebar/sidebar.tsx`, `profile-item.tsx` | 2 | — | |
| Sidebar: profile expand/collapse (collapsed on launch) | `stores/sidebar-store.ts` | 2 | — | |
| Sidebar: tab rows (favicon, title, loading, sleeping dimmed) | `components/sidebar/tab-item.tsx` | 2 | — | |
| Sidebar: drag to reorder profiles and tabs | `docs/superpowers/plans/2026-05-06-sidebar-reordering.md` | 2 | — | |
| Profile context menu: Edit, Keep loaded, Sign out of Google, Delete | `app/layout.tsx` | 2 (5 for sign-out) | — | |
| Tab context menu | `components/sidebar/tab-item.tsx` | 2 | — | |
| Toolbar: back, forward, reload/stop, address field, extension actions | `app/browser/_components/toolbar.tsx`, `browser-action-list.tsx` | 2 (4 for actions) | — | |
| Page framed in a rounded, inset card with a soft shadow | `components/content-panel.tsx` | 2 | — | |
| Empty state (no profile or tab) | `app/browser/_components/empty-state.tsx` | 2 | — | |
| Find bar (count, next, previous, Escape) | `app/browser/_components/find-bar.tsx` | 2 | — | |
| Tab loading progress | `docs/superpowers/plans/` (tab-loading-progress) | 2 | — | |
| Ctrl+Tab switcher, MRU, grouped by profile | `components/tab-switcher.tsx`, `tab-switcher-grouping.ts` | 2 | — | |
| Profile sheet: name, color, group | `sheets/profile-sheet.tsx`, `components/color-picker.tsx` | 2 | — | |
| Profile sheet: fingerprint (platform, presets, spoof toggle) | `sheets/profile-sheet.tsx`, `components/default-fingerprints.ts`, `host-webgl.ts` | 5 | — | |
| Profile sheet: proxy (type, host, port, user, password, paste `host:port:user:pass`, test) | `sheets/profile-sheet.tsx` | 5 | — | |
| Profile sheet: Keep loaded | `sheets/profile-sheet.tsx` | 5 | — | |
| Settings: Appearance (theme) | `app/settings/page.tsx` | 2 | — | |
| Settings: Browser (Chrome path, auto-detect) | `app/settings/page.tsx` | 5 | — | |
| Settings: Resources (sleep after, unload after, trim cache over) | `app/settings/_components/resource-settings.tsx` | 5 | — | |
| Settings: Extensions (install from Web Store URL/ID, list, uninstall) | `app/settings/_components/extension-settings.tsx` | 4 | — | |
| Settings: Security (set up, change, remove PIN) | `app/settings/page.tsx` | 5 | — | |
| Settings: Updates (check, download) | `app/settings/page.tsx`, `components/update-status-label.tsx` | 5 | — | |
| Lock screen with PIN numpad | `app/lock-screen/page.tsx`, `components/numpad.tsx` | 5 | — | |
| Update pill in the sidebar footer (dismissible per launch) | `components/sidebar/sidebar.tsx`, DESIGN.md | 5 | — | |

## Shortcuts (`apps/veil/src/main/window.ts`)

| Feature | Keys | Phase | Status | Evidence |
|---|---|---|---|---|
| New tab (focuses the address field) | ⌘T | 2 | — | |
| Close tab | ⌘W | 2 | — | |
| Reopen closed tab (last 20, across profiles) | ⇧⌘T | 2 | — | |
| Find in page | ⌘F | 2 | — | |
| Reload | ⌘R | 2 | — | |
| Page DevTools | ⇧⌘I | 3 | — | |
| App DevTools | ⌥⌘I | — | ⛔ | Electron-only: the app UI is native, so there is no app web UI to inspect. |
| Zoom in / out / reset | ⌘+ / ⌘− / ⌘0 | 2 | — | |
| Toggle full screen | ⌃⌘F | 2 | — | |
| Back / Forward | ⌘[ / ⌘] | 2 | — | |
| Focus address field | ⌘L | 2 | — | |
| Tab switcher forward / backward (MRU) | ⌃⇥ / ⌃⇧⇥ | 2 | — | |
| Go to tab 1–9 | ⌘1 … ⌘9 | 2 | — | |
| Check for updates (app menu) | — | 5 | — | |
| Edit menu (undo, redo, cut, copy, paste, select all) | standard | 2 | — | |

## Settings and persisted state (`apps/veil/src/stores/`)

| Feature | Electron source | Phase | Status | Evidence |
|---|---|---|---|---|
| `theme`: system / light / dark — chrome, menus, and `prefers-color-scheme` follow (ADR 0001) | `settings-store.ts` | 2 | — | |
| `tabSleepAfterMinutes` | `settings-store.ts` | 5 | — | |
| `profileUnloadAfterMinutes` | `settings-store.ts` | 5 | — | |
| `cacheBudgetMB` | `settings-store.ts` | 5 | — | |
| `chromiumPath` (Google sign-in fallback) | `settings-store.ts` | 5 | — | |
| Profiles: id, name, color, group, fingerprint, proxy, keepLoaded, tabs (id, url, title, favicon, isLoaded), timestamps — persisted | `profile-store.ts` (`profiles.json`) | 2 (5 for fingerprint/proxy) | — | |
| Active tab and profile, MRU history, closed-tab stack | `tab-store.ts` | 2 | — | |
| Expanded profiles (not persisted) | `sidebar-store.ts` | 2 | — | |
| Current page: browser or settings | `navigation-store.ts` | 2 | — | |
| PIN hash (bcrypt) and lock state | `security-store.ts` (`security.json`) | 5 | — | |
| Installed extensions per profile | `extension-store.ts` | 4 | — | |
| Proxy status per profile | `proxy-status-store.ts` | 5 | — | |
| Update status (checking, available, downloading) | `update-store.ts` | 5 | — | |
| Migration of all of the above from the Electron app | (new) | 5 | — | |

## Engine behavior

| Feature | Electron source | Phase | Status | Evidence |
|---|---|---|---|---|
| Per-profile isolation (cookies, storage, cache) | `profile/profile.ts` (`persist:profile-<id>`) | 2 | — | |
| Permission policy: clipboard writes allowed, the rest denied or prompted | `profile/profile.ts` | 3 | — | |
| Spellcheck suggestions and add to dictionary | `packages/electron-chrome-context-menu` | 3 | — | |
| Page context menu (copy, paste, image, links, inspect) | `packages/electron-chrome-context-menu` | 3 | — | |
| Downloads | `profile/profile-tabs.ts` | 2 | — | |
| Popups and `window.open` open as tabs | `profile/profile-tabs.ts` | 2 | — | |
| Renderer crash recovery | (new) | 2 | — | |
| Favicons | `profile/favicon.ts` | 2 | — | |
| Per-profile proxy, SOCKS5 with auth relay, WebRTC IP policy | `profile/proxy-relay.ts`, `profile.ts` | 5 | — | |
| Per-profile fingerprint and Client Hints | `profile/fingerprint-preload.ts`, `client-hints.ts` | 5 | — | |
| Google sign-in fallback (real Chrome + cookie import) | `profile/google/*` | 5 | — | |
| Sleep, Unload, Keep loaded, Trim | `sleep-scheduler.ts`, CONTEXT.md | 5 | — | |
| Chrome extensions (actions, popups, tabs/windows APIs) | `packages/electron-chrome-extensions` | 4 | — | |
| Chrome password manager and autofill prompts | — | 3 | — | Decision in Phase 3: off by default (Pane relies on extension password managers). |
