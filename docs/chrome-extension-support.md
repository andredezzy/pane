# Chrome Extension Support in Pane

## Overview

Pane supports Chrome MV3 extensions via a forked `electron-chrome-extensions` (ECE) package at `packages/electron-chrome-extensions/`. Extensions are loaded per-profile using isolated Electron sessions (`persist:profile-<id>`), giving each browser profile its own extension state, cookies, and storage.

**Tested extensions:**

| Extension | Status | Notes |
|-----------|--------|-------|
| Dark Reader | Popup fully functional | Uses `chrome.*` directly |
| 1Password | Popup fully functional | Uses `globalThis.browser \|\| globalThis.chrome`; exports `browser` wholesale if `.runtime.id` is truthy |
| NordPass | Fully working (login + vault popup) | Uses `globalThis.browser \|\| globalThis.chrome`; accesses `chrome.action.onClicked` at module level |
| uBlock Origin | Untested | Likely needs `webRequest` + `declarativeNetRequest` |

## The V8 Chrome Proxy Problem

This is the foundational constraint shaping the entire architecture.

### What happens

In Electron's service worker contexts, `globalThis.chrome` is a **V8 host-defined Proxy** with non-configurable properties. Calling `Object.defineProperty` on this Proxy silently corrupts it:

```
Object.defineProperty(chrome, 'action', { value: {...}, configurable: true })
// Silently modifies the Proxy's target but NOT its internal get trap state.
// Subsequent reads of NON-CONFIGURABLE properties trigger:
TypeError: 'get' on proxy: property 'runtime' is a read-only and non-configurable
data property on the proxy target but the proxy did not return its actual value
```

### What we verified through testing

| Operation | Frame context | SW context |
|-----------|--------------|------------|
| `Object.defineProperty(chrome, 'action', ...)` | Works (chrome is a regular object) | Silently fails AND corrupts the Proxy |
| `chrome.action = {...}` (assignment) | Works | Silently rejected by V8 Proxy set trap |
| `chrome.tabs.onRemoved = makeEvent(...)` (sub-object) | Works | Silently rejected — sub-objects are also immutable bindings |
| `Object.freeze(chrome)` | Works | On our empty-target Proxy: makes V8 enforce that non-existent properties return `undefined`, killing extras |
| Reading `chrome.runtime` after corruption | Works | Throws invariant error ONLY when accessed through a user-created Proxy wrapping chrome |
| Reading `chrome.runtime` after corruption (direct) | N/A | Works — V8 doesn't check its own Proxy's invariant |

### Key discoveries

1. **`Object.defineProperty` corrupts ALL properties**, not just the one being defined. Defining `chrome.action` makes `chrome.runtime` and `chrome.windows` throw.
2. **Direct property reads still work** after corruption. `chrome.runtime.sendMessage()` works fine when accessed directly. The invariant error only triggers when accessed through a USER-created Proxy wrapping chrome.
3. **`globalThis.chrome` cannot be reassigned in general** — it's a V8 binding. But it CAN be reassigned inside `executeInMainWorld` if done before any extension code runs.
4. **`Object.freeze` on a Proxy with empty target** prevents the get trap from returning properties not on the target. This killed our extras when `injectExtensionAPIs` froze our Proxy.
5. **Sub-object modification is also rejected** in SW context. `chrome.tabs.onRemoved = ...` silently fails. The V8 Proxy returns immutable binding wrappers for sub-objects.
6. **The upstream `electron-browser-shell` has the same limitation.** Dark Reader works there, NordPass doesn't. The upstream `injectExtensionAPIs` silently fails to install APIs in SW but extensions work via native chrome APIs.
7. **`chrome.runtime` returns different wrapper objects each time** it's accessed through the V8 Proxy after corruption. This means `new Proxy(chrome, { get(t,k) { return t[k]; } })` triggers invariant errors because the get trap reads `t[k]` twice (once for the return, once for the invariant check) and gets different objects.

### What DOES NOT work

- `Object.defineProperty(chrome, ...)` — corrupts the Proxy
- `chrome.newProp = value` — silently rejected
- `chrome.existingProp.newMethod = func` — silently rejected (sub-objects are binding wrappers)
- `new Proxy(chrome, {...})` — invariant violations after corruption
- `Object.freeze(ourProxy)` when target is `{}` — kills get trap results for non-existent properties

### What DOES work

- Reading native properties: `chrome.runtime`, `chrome.storage`, `chrome.tabs` (direct access)
- Native messaging: `chrome.runtime.sendMessage`, `chrome.runtime.onMessage`, `chrome.runtime.connect`
- Reassigning `globalThis.chrome` inside `executeInMainWorld` (before extension code)
- `new Proxy({}, {...})` — empty target, no invariant checks
- Caching native API references before corruption: `var runtime = chrome.runtime` preserves a valid reference

## Current Architecture

### Preload registration order

```
Service Worker context (order matters!):
  1. crx-sw       → sw.js                          (caches natives BEFORE corruption)
  2. crx-api-sw   → chrome-extension-api.preload.js (runs injectExtensionAPIs, corrupts chrome)

Frame context (order doesn't matter — chrome is a regular object):
  1. crx-api-frame → chrome-extension-api.preload.js (injectExtensionAPIs succeeds)
  2. crx-frame     → frame.js                        (process polyfill, patches)
```

**Why sw.js runs FIRST in SW context:** sw.js caches `chrome.runtime`, `chrome.storage`, `chrome.tabs`, etc. from the UNCORRUPTED V8 Proxy. Then `injectExtensionAPIs` runs and corrupts chrome — but our cached references are already safe.

**Why `crx-api-sw` exists (even though it corrupts chrome):** Removing it breaks Dark Reader and 1Password. The exact mechanism is unclear — `injectExtensionAPIs` fails to install any APIs on the V8 Proxy, but some side effect of running it (possibly the `electron` IPC bridge setup or the `crx-msg` channel registration) is required for popup↔SW messaging to work.

### File structure

```
packages/electron-chrome-extensions/
├── src/
│   ├── browser/
│   │   ├── index.ts           # Main class — registers preloads, IPC, event handlers
│   │   ├── api/               # Chrome API handlers (crx-msg channel)
│   │   │   ├── browser-action.ts
│   │   │   ├── tabs.ts
│   │   │   ├── windows.ts
│   │   │   ├── web-navigation.ts
│   │   │   ├── context-menus.ts
│   │   │   ├── runtime.ts
│   │   │   ├── cookies.ts
│   │   │   ├── notifications.ts
│   │   │   ├── commands.ts
│   │   │   └── permissions.ts
│   │   ├── shims/             # Chrome API shims (crx-shim channel, SW-specific)
│   │   │   ├── handler.ts     # IPC dispatcher: ipcMain + per-SW ipc.handle
│   │   │   ├── alarms.ts      # chrome.alarms (setTimeout-based)
│   │   │   ├── idle.ts        # chrome.idle (powerMonitor-based)
│   │   │   ├── windows.ts     # chrome.windows.create → createTab
│   │   │   └── tabs.ts        # chrome.tabs.create/query → store
│   │   ├── popup.ts           # PopupView (frameless BrowserWindow)
│   │   ├── router.ts          # IPC routing for crx-msg channel
│   │   ├── store.ts           # Tab/window state tracking
│   │   └── context.ts         # ExtensionContext type
│   ├── renderer/
│   │   ├── index.ts           # injectExtensionAPIs() — main world API injection
│   │   └── event.ts           # Extension event listener management
│   ├── preloads/
│   │   ├── sw.js              # SW preload: process polyfill + NordPass Proxy
│   │   └── frame.js           # Frame preload: patches + blank popup detection
│   └── preload.ts             # Entry point — calls injectExtensionAPIs()
├── esbuild.config.cjs         # Builds preloads to dist/preloads/
└── dist/
    ├── chrome-extension-api.preload.js  # Built from preload.ts
    ├── cjs/index.js
    └── preloads/
        ├── sw.js
        └── frame.js
```

### Two IPC channels

| Channel | Registered by | Used by | Handlers |
|---------|--------------|---------|----------|
| `crx-msg` | ECE router (router.ts) | `injectExtensionAPIs` (frame preload) | TabsAPI, WindowsAPI, BrowserActionAPI, RuntimeAPI, etc. |
| `crx-shim` | handler.ts | sw.js (SW preload) | alarms, idle, windows, tabs, action.setPopup |

Both channels are registered on each SW via `running-status-changed` at status `"starting"`.

## Service Worker Preload (`sw.js`)

### Execution flow

```
1. [Isolated world] Expose __crxIpc via contextBridge.exposeInMainWorld
2. [Isolated world] Register crx-shim-event listener for event relay
3. [Main world via executeInMainWorld]:
   a. Set globalThis.process polyfill
   b. Read chrome.runtime.id to detect extension
   c. If NordPass: cache native APIs, create empty-target Proxy, set chrome = proxy
   d. If other extension: return (no Proxy, no chrome replacement)
```

### NordPass-specific Proxy

NordPass requires special handling because:
1. It accesses `chrome.action.onClicked` at **module level** (line 5148 of background.js) — before any `this.browser = browser || chrome` assignment
2. `chrome.action` is undefined in Electron's SW context
3. The V8 Proxy rejects all attempts to add it via assignment or defineProperty

Solution: replace `globalThis.chrome` with our Proxy (empty target, cached natives + extras) ONLY for NordPass. Other extensions keep the original chrome.

```js
// Why empty target: new Proxy({}, ...) has no non-configurable properties,
// so V8 never checks the invariant on our get trap.
//
// Why cached natives: chrome.runtime returns different wrapper objects each
// time after corruption. Caching gives stable references.
//
// Why chrome replacement: NordPass accesses chrome.action.onClicked at
// module level, before it can use globalThis.browser.
globalThis.browser = new Proxy({}, { get(t, k) {
  if (k in extras) return extras[k];     // action, contextMenus, etc.
  if (k in cached) return cached[k];     // runtime, storage, tabs, etc.
  try { return oc[k]; } catch { return undefined; }
}});
globalThis.chrome = proxy;  // NordPass only
```

### Why 1Password breaks with globalThis.browser

1Password's background.js (line ~366684) checks:
```js
if (globalThis.browser && globalThis.browser.runtime && globalThis.browser.runtime.id)
  module.exports = globalThis.browser;
```

If `globalThis.browser` exists with a valid `runtime.id`, 1Password exports the ENTIRE Proxy as its API object. ALL subsequent API calls go through our Proxy instead of native chrome. Native method bindings break because they're accessed through the Proxy (different `this` context, V8 invariant issues, etc.).

This is why `globalThis.browser` is only set for NordPass (detected by extension ID).

### Why Dark Reader doesn't need the Proxy

Dark Reader's background.js uses `chrome.*` directly (grep confirms 0 references to `globalThis.browser`). It only checks `typeof browser !== "undefined"` for Firefox theme API compatibility. Since we don't set `globalThis.browser` for Dark Reader, the check fails and it stays on `chrome.*`.

### Extras provided by the Proxy

| API | Methods | Events |
|-----|---------|--------|
| `action` / `browserAction` | setPopup (IPC), setIcon/setTitle/setBadgeText (no-op) | onClicked (makeEvent) |
| `alarms` | create/get/getAll/clear/clearAll (IPC via crx-shim) | onAlarm (makeEvent) |
| `idle` | setDetectionInterval/queryState (IPC) | onStateChanged (makeEvent) |
| `windows` | create/get/getCurrent/getAll/update/remove (IPC) | onCreated/onRemoved/onFocusChanged (no-op) |
| `contextMenus` | create/remove/removeAll (stubs) | onClicked (no-op) |
| `privacy` | network/services/websites (safe defaults) | — |
| `webNavigation` | getFrame/getAllFrames (stubs) | onCommitted/onCompleted/etc. (no-op) |
| `offscreen` | createDocument/closeDocument/hasDocument (stubs) | — |

## Frame Preload (`frame.js`)

Runs in ALL extension pages (popups, options, app.html).

### What it does

1. **Process polyfill** — `globalThis.process = { env: { NODE_ENV: "production" }, ... }`
2. **chrome.extension stubs** — `getViews`, `isAllowedIncognitoAccess`, `isAllowedFileSchemeAccess`
3. **window.close() blocking** — prevents extensions from destroying the popup BrowserWindow
4. **chrome.windows.create / chrome.tabs.create patches** — IPC-backed implementations for frame contexts
5. **Blank popup detection** — after 1.5s, checks DOM element count. Skips for extensions with `default_popup` declared. Opens app.html for blank popups (NordPass unauthenticated flow).

### Blank popup detection details

```js
// Skip if viewport is large (not a popup)
if (window.innerWidth > 500 || window.innerHeight > 600) return false;
// Skip if extension declares default_popup (intentional popup UI)
if (manifest.action?.default_popup || manifest.browser_action?.default_popup) return false;
// Blank = fewer than 3 DOM elements in #app or body
return app.querySelectorAll("*").length <= 3;
```

This was modified because the original threshold (3 elements) false-positived on Dark Reader's "Loading, please wait" spinner, opening app.html as a tab alongside the popup.

## `injectExtensionAPIs()` — The Upstream Preload

Located in `src/renderer/index.ts`, called from `src/preload.ts`. Built to `dist/chrome-extension-api.preload.js`.

### How it works

1. Exposes `electron` IPC bridge via `contextBridge.exposeInMainWorld`
2. Runs `mainWorldScript` via `executeInMainWorld`:
   - Reads `chrome.runtime.id` and `chrome.runtime.getManifest()`
   - Creates API factory definitions (tabs, windows, action, runtime, etc.)
   - For each API: `Object.defineProperty(chrome, apiName, { value: factory(baseApi) })`
   - Deletes `globalThis.electron`
3. Does NOT call `Object.freeze(chrome)` (removed — freezing our empty-target Proxy broke extras)

### In frame contexts

`Object.defineProperty(chrome, ...)` succeeds because `chrome` is a regular object in frames. All APIs are installed. The popup gets full IPC-backed Chrome APIs:
- `chrome.tabs.query` → `ipcRenderer.invoke('crx-msg', extensionId, 'tabs.query', ...)` → ECE router → TabsAPI
- `chrome.tabs.onCreated` → `ExtensionEvent('tabs.onCreated')` → IPC-based event listener
- `chrome.runtime.sendMessage` → preserved from native via `...base` spread

### In SW contexts

ALL `Object.defineProperty(chrome, ...)` calls fail silently (V8 Proxy). The function is a no-op in terms of API installation. But some side effect of running it is required for Dark Reader and 1Password popup communication to work. Removing `crx-api-sw` registration breaks both extensions — the exact mechanism is under investigation.

### The `nativeApis` guard (REMOVED)

The old fork had a `nativeApis` set that skipped `Object.defineProperty` for `runtime`, `storage`, `tabs`, `scripting`, `management`, `extension`, `webRequest`, `devtools`. This was added to prevent Proxy invariant corruption. It was removed because:
1. In frame contexts, `Object.defineProperty` works — the guard prevented legitimate API injection
2. In SW contexts, ALL defineProperty calls fail anyway — the guard is redundant
3. Removing it allowed `injectExtensionAPIs` to install full IPC-backed APIs in popups, which is required for Dark Reader and 1Password popup↔SW messaging

## Extension-Specific Behaviors

### Dark Reader (`eimadpbcbfnmbkopoojfekhnkhdbieeh`)

**API pattern:** `chrome.*` directly. No `globalThis.browser` usage.

**SW errors (non-fatal):**
- `chrome.tabs.onRemoved` undefined — can't add events to native tabs binding
- `chrome.storage.sync` not available — Electron doesn't support sync storage
- `chrome.tabs.onRemoved` null → `applyToListedOnly` — secondary error from missing event

**What works:** Popup fully functional. Popup sends `chrome.runtime.sendMessage` to SW, SW responds via native `chrome.runtime.onMessage`. The popup's `chrome.tabs.query` goes through `injectExtensionAPIs`'s IPC-backed implementation (installed in frame context).

### 1Password (`aeblfdkhhhdcdjpifhhbdiojplfjncoa`)

**API pattern:** `globalThis.browser || globalThis.chrome`. Exports `browser` wholesale if `browser?.runtime?.id` is truthy.

**SW errors:**
- `chrome.action.onClicked` undefined → Status code 15 (SW crash on second load). Non-fatal for popup — the first load partially succeeds and native `chrome.runtime` messaging works.
- `[LogManager] Failed to load config` — non-fatal internal error

**What works:** Popup fully functional. Same messaging pattern as Dark Reader.

**What breaks it:** Setting `globalThis.browser` to ANY Proxy with a valid `runtime.id`. 1Password exports the Proxy as its API module, routing ALL calls through it. Native method bindings break (different `this` context, V8 invariant issues).

### NordPass (`eiaeiblijfjekdanodkjadfinkhbfgcd`)

**API pattern:** `globalThis.browser || globalThis.chrome` at class level. `chrome.action.onClicked` at module level (line 5148).

**Why it needs special handling:**
1. Accesses `chrome.action.onClicked` at module level — before any `browser || chrome` assignment
2. `chrome.action` doesn't exist natively in Electron SW context
3. V8 Proxy rejects all modifications to chrome
4. Only way to provide `chrome.action.onClicked`: replace `globalThis.chrome` with our Proxy

**Current state:** Fully working. SW boots, app.html navigates to `#/login`, OAuth flow completes, popup shows vault after auth.

**What fixed it:** The NordPass Proxy's extras (alarms, windows, action.setPopup) were calling `__crxIpc.invoke(...)` but `__crxIpc` was never exposed to the main world — `contextBridge.exposeInMainWorld("__crxIpc", ...)` was missing from sw.js. All IPC-backed API calls silently returned nothing, preventing NordPass's SW from completing initialization (it needs working `alarms.create` for keepalive timers). Additionally, the `crx-shim-event` relay was missing, so events from the main process (alarms.onAlarm, idle.onStateChanged) never reached the SW. Finally, `handler.ts` used `getAllExtensions()[0]` to identify the calling extension for `action.setPopup`, which returned the wrong extension when multiple extensions were loaded — fixed by extracting the extension ID from the SW's scope URL.

#### NordPass SW initialization (from working version analysis)

NordPass's SW (`background.js`) is ~4MB minified. It:

1. Uses `globalThis.browser || globalThis.chrome` to get the chrome API object — our `globalThis.browser` Proxy is picked up automatically
2. Initializes a Redux store with slices for auth, vault, settings, route, etc.
3. `bgAppState` transitions from `"LOADING"` to `"READY"` when two internal initialization flags are both true
4. Communicates with the popup via a custom **synco-redux** port-based state sync system

#### Synco-redux state sync

NordPass uses a custom Redux sync system between SW and popup:

- **Port name:** `"synco-redux"`
- **Three message types:** `PATCH_STATE` (incremental), `SYNC_GLOBAL` (full state), `DISPATCH_ACTION`
- Popup connects → sends `SYNC_GLOBAL` → SW responds with full Redux state → popup's `isStateSynced` becomes `true` → React renders

We verified this works via `executeJavaScript` probing: the popup's Redux store shows `{ isStateSynced: true, bgAppState: "READY", authState: "unauthenticated" }`. The synco-redux port system works through native `chrome.runtime.connect` which Electron provides.

#### Popup routing

NordPass's popup (`index.html`) uses a `HashRouter` that defaults ALL routes to `/validate-master-password` via a catch-all `path: "*"` redirect. The `validate-master-password` route component renders blank when `authState !== "master_validate"`.

The route guard that should redirect `unauthenticated → /login` is in the `Kf` component. For `authState === "unauthenticated"`, `Kf` passes through to children, but the children's route component (for `validate-master-password`) renders nothing — so the popup is blank.

**Key insight: The popup has NO login UI.** NordPass delegates login to a separate window (`app.html`). This is why the blank popup detection + fallback to `app.html` is essential.

#### Login flow (when working)

1. Click NordPass icon → no `default_popup` → blank popup detected → `browser-action-clicked` → opens `app.html` as tab
2. `app.html` connects to SW via `chrome.runtime.connect({name: "synco-redux"})`
3. SW sends Redux state → app.html React renders
4. app.html router navigates to `#/login` (based on `authState: "unauthenticated"`)
5. User enters email → clicks Continue → navigates to `nordaccount.com` for OAuth
6. After OAuth, NordPass SW sets `authState: "authenticated"` and calls `action.setPopup({popup: 'index.html'})`
7. Next icon click opens the popup with the vault UI

#### Translation loading

NordPass loads translations via `fetch(chrome.runtime.getURL('assets/lang/${locale}.json'))`. These JSON files are bundled in the extension directory. The `fetch` works correctly in Electron extension contexts via the CRX protocol handler registered by `ElectronChromeExtensions.handleCRXProtocol(session)`.

#### Process global

NordPass's `background.js` at line ~5170 has `process.getBuiltinModule?.("node:os")` — a bare `process` reference that would throw `ReferenceError` in a service worker. Our preload sets `globalThis.process = { env: { NODE_ENV: "production" }, platform: "darwin", version: "" }` before the SW script runs, which satisfies this check (the optional chaining on `getBuiltinModule` handles the missing method).

#### What changed between working and current state

| Aspect | Old (NordPass working, DR/1PW broken) | Current (all three working) |
|--------|----------------------|---------------------------|
| SW preload | Full Proxy `new Proxy(oc, ...)` wrapping chrome | Empty-target `new Proxy({}, ...)` with cached natives (NordPass only) |
| `globalThis.chrome` | Replaced for ALL extensions | Replaced for NordPass ONLY |
| `injectExtensionAPIs` in SW | Not registered | Registered (crx-api-sw) — corrupts chrome but side effect needed |
| `injectExtensionAPIs` in frame | Not registered | Registered (crx-api-frame) — installs APIs |
| `__crxIpc` + event relay | Active for ALL SWs | Active for ALL SWs |
| `action.setPopup` IPC | via `oc.action.setPopup` intercept | via Proxy extras + `crx-shim` dispatch with per-SW extension ID |
| Dark Reader | Broken (infinite loading) | Working |
| 1Password | Broken (Status 15) | Working |
| NordPass | Working (login flow complete) | Working (login + vault popup) |

## Approaches That Failed

### 1. `globalThis.chrome = globalThis.browser` for ALL extensions

**What:** Replace `globalThis.chrome` with our Proxy for all extensions, not just NordPass.

**Why it failed:** 1Password checks `globalThis.browser?.runtime?.id` and exports the Proxy wholesale. All subsequent API calls go through the Proxy, breaking native method bindings. Dark Reader popup↔SW messaging also broke — native `chrome.runtime.sendMessage` stops working when chrome is the Proxy.

### 2. `new Proxy(chrome, {...})` as the browser Proxy

**What:** Wrap the chrome V8 Proxy with our own Proxy.

**Why it failed:** After `injectExtensionAPIs` corrupts chrome, `chrome.runtime` returns different wrapper objects on each access. V8's invariant check on our outer Proxy detects the mismatch and throws. Error: `'get' on proxy: property 'runtime' is a read-only and non-configurable data property on the proxy target but the proxy did not return its actual value`.

### 3. Native-first Proxy (merge extras with native)

**What:** Proxy that checks native first, only falls back to extras for missing properties. Shallow-copied native methods to merged object.

**Why it failed:** Copying native methods (`merged.create = native.create`) breaks V8 binding context. Native Chrome API methods lose their internal binding when called from a different object. 1Password's `alarms.create` failed silently.

### 4. Nested Proxies (Proxy wrapping native sub-objects)

**What:** For each namespace, return a Proxy wrapping the native sub-object that fills gaps from extras.

**Why it failed:** Same 1Password issue — `globalThis.browser` existing with valid `runtime.id` causes 1Password to export it.

### 5. Augmenting chrome sub-objects directly

**What:** `chrome.tabs.onRemoved = makeEvent(...)`, `chrome.action = {...}` via assignment.

**Why it failed:** The V8 chrome Proxy rejects ALL modifications — both `Object.defineProperty` AND direct assignment AND sub-object property assignment. Every write is silently dropped.

### 6. Removing `crx-api-sw` (no `injectExtensionAPIs` in SW)

**What:** Only run sw.js in SW context, no `injectExtensionAPIs`.

**Why it failed:** Dark Reader and 1Password popups break. The exact mechanism is unknown — `injectExtensionAPIs` fails to install any APIs on the V8 Proxy, but some side effect is required. Possibly the `electron` IPC bridge exposure or `crx-msg` channel setup.

### 7. Adding `alarms`/`idle` to `apiDefinitions` in renderer/index.ts

**What:** Provide alarms and idle stubs via `injectExtensionAPIs` instead of sw.js.

**Why it failed:** Replaced working native `chrome.alarms` (Electron provides native alarms) with broken IPC stubs routed to `crx-msg` (which has no alarms handler — alarms are on `crx-shim`). Dark Reader uses `chrome.alarms` and broke immediately.

### 8. `Object.freeze(chrome)` after API injection

**What:** Upstream pattern — freeze chrome after installing APIs.

**Why it failed:** When chrome is our empty-target Proxy (`Proxy({}, ...)`), freezing it makes the target non-extensible. V8 then enforces that the get trap returns `undefined` for properties not on the target — killing ALL our extras (action, contextMenus, etc.).

## IPC Architecture

### Channel routing

```
Frame context (popup/options/app.html):
  chrome.tabs.query(...)
    → invokeExtension(extensionId, 'tabs.query', ...)
    → ipcRenderer.invoke('crx-msg', extensionId, 'tabs.query', ...)
    → ipcMain.handle('crx-msg', ...) [ECE router]
    → TabsAPI.query handler
    → Response to popup

SW context (NordPass via browser Proxy):
  browser.windows.create({url: 'app.html'})
    → extras.windows.create({url: 'app.html'})
    → __crxIpc.invoke("windows", "create", {url: 'app.html'})
    → ipcRenderer.invoke("crx-shim", "windows", "create", ...)
    → sw.ipc.handle("crx-shim", ...) [handler.ts, per-SW]
    → handleWindows(ctx, "create", {url: 'app.html'})
    → ctx.store.createTab({url})
    → Tab opens in Pane

Popup ↔ SW messaging (native, not shimmed):
  popup: chrome.runtime.sendMessage({type: 'getData'})
    → Electron native IPC
    → SW: chrome.runtime.onMessage listener fires
    → SW: sendResponse(data)
    → popup: callback receives data
```

## How to Build and Test

```bash
cd /path/to/pane

# Rebuild ECE fork (after any changes to packages/electron-chrome-extensions/src/)
cd packages/electron-chrome-extensions && node esbuild.config.cjs

# Launch in development
cd apps/desktop && npx electron-vite dev

# Clean test (fresh profile data)
rm -rf "$HOME/Library/Application Support/@pane"
```

### Dev auto-install

In development mode (`NODE_ENV !== "production"`), three test extensions auto-install on startup:
- Dark Reader (`eimadpbcbfnmbkopoojfekhnkhdbieeh`)
- 1Password (`aeblfdkhhhdcdjpifhhbdiojplfjncoa`)
- NordPass (`eiaeiblijfjekdanodkjadfinkhbfgcd`)

This is configured in `apps/desktop/src/main/pane.ts` in the `restore()` method.

### Testing checklist

1. Create a profile → extensions auto-load → icons appear in address bar
2. Click Dark Reader icon → popup shows with toggle, site list, settings
3. Click 1Password icon → popup shows vault items
4. Click NordPass icon (unauthenticated) → `app.html` opens → should show login form (currently shows spinner)

### Debugging SW issues

- **Console output:** SW `console.log` goes to the SW's devtools, NOT the terminal. Use `ipcRenderer.invoke("crx-shim", "__log", message)` from the SW preload to log to the main process terminal.
- **SW errors:** Check the terminal for `Extension Error` messages with the extension ID.
- **SW database corruption:** If SW fails to start with `DidStartWorkerFail: 5`, delete: `rm -rf "$HOME/Library/Application Support/@pane/desktop/Partitions/profile-<id>/Service Worker"`

## Gotchas

1. **NEVER use `Object.defineProperty` on the `chrome` Proxy.** It silently corrupts ALL non-configurable properties. Use sub-object modification (in frame contexts) or the empty-target Proxy pattern (in SW contexts).

2. **NEVER use `Object.freeze` on the empty-target Proxy.** It makes V8 enforce that non-existent properties return undefined, killing the get trap's ability to return extras.

3. **NEVER set `globalThis.browser` for ALL extensions.** 1Password checks `globalThis.browser?.runtime?.id` and exports it wholesale, breaking native method bindings.

4. **NEVER add native APIs (alarms, idle) to `apiDefinitions` in renderer/index.ts.** Electron provides these natively. Our factories replace them with IPC stubs routed to `crx-msg` (which has no handler for alarms/idle — those are on `crx-shim`).

5. **Preload order matters in SW context.** sw.js MUST run BEFORE `chrome-extension-api.preload.js` so it can cache native API references before `injectExtensionAPIs` corrupts the V8 Proxy.

6. **Service worker IPC uses `serviceWorker.ipc.handle()`** — NOT `ipcMain.handle()`. Each SW has its own IPC channel in Electron 41.

7. **Preload path resolution when bundled:** `__dirname` points to the app's output directory. ECE uses `createRequire(process.cwd()).resolve('@pane/electron-chrome-extensions')` to find the package. The `chrome-extension-api.preload.js` is at `path.join(preloadsDir, '..', 'chrome-extension-api.preload.js')`.

8. **SW preload `console.log` doesn't appear in terminal.** Use the `__log` handler: `ipcRenderer.invoke("crx-shim", "__log", "message")` from the isolated world.

9. **Extension files are NOT modified on disk.** Everything is via preloads. Essential for CWS auto-updates.

## Remaining Work

### General extension compatibility

The current architecture is extension-ID-specific for NordPass (`eiaeiblijfjekdanodkjadfinkhbfgcd`). A general solution would:
1. Detect extensions that need `globalThis.browser` based on their code patterns, not hardcoded IDs
2. Determine which extensions break with `globalThis.browser` (like 1Password) and exclude them
3. Potentially use per-extension preload configurations via a manifest or registry

### Understanding `crx-api-sw` dependency

Dark Reader and 1Password break when `crx-api-sw` (injectExtensionAPIs in SW) is removed, even though all its `Object.defineProperty` calls fail silently. The side effect that makes them work needs to be identified and isolated — possibly the `electron` IPC bridge, the `crx-msg` channel setup, or a V8 state change from running `executeInMainWorld`.
