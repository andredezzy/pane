# Generic Extension Shim Architecture

**Date:** 2026-05-01
**Status:** Approved
**Branch:** feat/extension-support

## Problem

Pane's current extension support modifies extension files on disk — prepending shim code to `background.js` and injecting scripts into popup HTML. This approach:

1. Breaks when extensions auto-update from Chrome Web Store (files get replaced)
2. Contains NordPass-specific logic (intercepting `DESKTOP/OPEN` messages, blank popup detection)
3. Requires bundling extension files in the repo (`apps/desktop/extensions/`)
4. Uses fragile string-template code generation for the service worker shim

## Solution

Move ALL shimming into Electron preload scripts registered on the extension session. Extensions are never modified on disk. Any MV3 extension loaded via `session.extensions.loadExtension()` gets the same generic treatment automatically.

## Architecture

### File Structure

```
apps/desktop/src/main/extensions/
├── extension-manager.ts          # Load extensions, manage sessions, NO patching
└── shims/
    ├── handler.ts                # Per-SW IPC registration + dispatch
    ├── alarms.ts                 # chrome.alarms (unchanged)
    ├── idle.ts                   # chrome.idle (unchanged)
    ├── windows.ts                # chrome.windows → TabManager (NEW)
    └── tabs.ts                   # chrome.tabs → TabManager (NEW)

apps/desktop/resources/
├── sw-preload.js                 # IPC bridge + globalThis.browser Proxy + all extras
└── shim-preload.js               # chrome.extension stubs, process global
```

### Deleted

```
apps/desktop/extensions/eiaeiblijfjekdanodkjadfinkhbfgcd/   # entire bundled extension
apps/desktop/resources/noop-preload.js                       # unused
extension-manager.ts: patchJsFile()                          # on-disk SW patching
extension-manager.ts: patchPopupHtml()                       # on-disk popup patching
extension-manager.ts: patchExtensionScripts()                # orchestrator for patching
extension-manager.ts: autoLoadExtensions()                   # bundled extension loader
extension-manager.ts: pane-open-tab IPC handler              # NordPass-specific
shim-preload.js: __paneFrame.openTab()                       # NordPass-specific
shim-preload.js: window.close() blocking                     # NordPass-specific
```

## SW Preload (`sw-preload.js`)

The centerpiece of the architecture. Registered as `type: "service-worker"` on each extension session. Runs before any extension SW script.

### Globals Set

| Global | Purpose |
|--------|---------|
| `globalThis.process` | `{ env: { NODE_ENV: "production" }, platform: "darwin", version: "" }` — some extensions check this |
| `globalThis.__paneIpc` | `{ invoke(namespace, method, ...args) }` — IPC bridge to main process |
| `globalThis.__paneEvents` | `function(ns, evt, data)` — dispatches events from main process to listener arrays |
| `globalThis.browser` | Proxy around `chrome` with extras map for missing APIs |

### The `browser` Proxy

Extensions that support both Chrome and Firefox use `globalThis.browser || globalThis.chrome`. By setting `globalThis.browser` before the extension script runs, extensions pick up our Proxy which wraps `chrome` but intercepts missing APIs.

The Proxy's `get` trap:
1. If the key is in `extras` → return the shim object
2. If the key is in `patches` and native value exists → return merged object
3. Otherwise → `try { return chrome[key] } catch(e) { return undefined }`

The try/catch is necessary because the Chrome Proxy in Electron has non-configurable properties that can throw on access if corrupted by `Object.defineProperty` (see Proxy Invariant section below).

### Extras Map

| API | Behavior |
|-----|----------|
| `action` / `browserAction` | No-ops — ECE handles the real action lifecycle via its own IPC |
| `alarms` | `__paneIpc.invoke("alarms", method, ...)` → main-process setTimeout-based timers |
| `idle` | `__paneIpc.invoke("idle", method, ...)` → main-process `powerMonitor.getSystemIdleTime()` |
| `windows` | `__paneIpc.invoke("windows", method, ...)` → main-process TabManager (NEW) |
| `tabs` | `__paneIpc.invoke("tabs", method, ...)` → main-process TabManager (NEW) |
| `contextMenus` | Stub — resolved Promises, no-op event listeners |
| `privacy` | Stub — hardcoded privacy setting values |

### Event Listeners

Each API's event objects (e.g., `alarms.onAlarm`, `idle.onStateChanged`) support real `addListener`/`removeListener`. When the main process fires an event via `sw.send("pane-shim-event", namespace, eventName, data)`, the preload receives it and dispatches to `__paneEvents`, which routes to the correct event's listener array.

## Frame Preload (`shim-preload.js`)

Registered as `type: "frame"` on each extension session. Runs in ALL extension pages (popups, options pages, `app.html`, etc.).

Uses `contextBridge.executeInMainWorld` to inject into the main world:

- `globalThis.process` — same stub as SW preload
- `chrome.extension.getViews = () => []`
- `chrome.extension.isAllowedIncognitoAccess = () => Promise.resolve(false)`
- `chrome.extension.isAllowedFileSchemeAccess = () => Promise.resolve(false)`

No `window.close()` blocking. No `__paneFrame` bridge. No tab-opening logic. Extensions control their own popup lifecycle.

## New IPC Handlers

### `windows.ts`

Handles `chrome.windows.create/get/getCurrent/getLastFocused/getAll/update/remove` from the SW context.

```
handleWindows(ses: Session, method: string, ...args)
```

Key methods:

- **`create({ url, type, ... })`** — resolves the profile from the session, creates a tab via `TabManager.createViewForExtension()`, returns a `chrome.windows.Window` object. This is what makes NordPass's `OPEN_LOGIN_WINDOW` handler work natively — it calls `chrome.windows.create({url: 'app.html'})` and our handler opens it as a Pane tab.

- **`get/getCurrent/getLastFocused`** — returns the main Pane window as a `chrome.windows.Window` object.

- **`getAll`** — returns `[mainWindow]`.

- **`update/remove`** — update returns the window object, remove is a no-op or closes the tab.

### `tabs.ts`

Handles `chrome.tabs.create/query/update/remove/get` from the SW context.

```
handleTabs(ses: Session, method: string, ...args)
```

Key methods:

- **`create({ url, active })`** — creates a tab via TabManager, returns a `chrome.tabs.Tab` object.

- **`query({ active, currentWindow, ... })`** — queries TabManager for matching tabs, returns array of `chrome.tabs.Tab` objects.

- **`update(tabId, { url, active })`** — navigates or activates a tab.

- **`remove(tabId)`** — closes a tab via TabManager.

- **`get(tabId)`** — returns a single tab's info.

### Handler Registration

`handler.ts` dispatch adds two new cases:

```typescript
case "windows":
  return handleWindows(ses, method, ...args);
case "tabs":
  return handleTabs(ses, method, ...args);
```

Both handlers need access to `TabManager`, the main `BaseWindow`, and `ProfileStore`. `registerShimHandlerForSession(ses, deps)` accepts a deps object with these references, and the dispatch function closes over them. The `Session` is resolved from the IPC event (same as existing handlers).

For URL resolution in `windows.create` and `tabs.create`: relative URLs (e.g., `app.html`) are resolved to `chrome-extension://<id>/<url>` using `ses.extensions.getAllExtensions()` to find the extension ID for the current session's scope.

## Simplified `ExtensionManager`

### Removed
- `patchExtensionScripts()` / `patchJsFile()` / `patchPopupHtml()` — all on-disk patching
- `autoLoadExtensions()` — no bundled extensions
- `EXTENSIONS_DIR` constant — no bundled extension directory
- `pane-open-tab` IPC handler — replaced by generic `windows.create`
- `findProfileForSession()` — moves to shared utility used by `windows.ts`/`tabs.ts`

### Kept
- `getOrCreateInstance()` — registers preloads, creates ECE instance, registers IPC handlers per session
- `registerTab()` / `activateTab()` / `unregisterTab()` — tab lifecycle hooks for ECE
- `loadExtension(profileId, path)` — loads from any path
- `destroyProfile()` — cleanup (alarms, idle, store)
- `registerIpc()` — `extensions:list`, `extensions:load`

### Added
- `unloadExtension(profileId, extensionId)` — for extension removal/disable

## ECE Fork

No changes needed for this refactor. Current state:

- **`src/browser/index.ts`** — both frame and SW preloads disabled (Proxy invariant fix). Stays as-is.
- **`src/renderer/index.ts`** — `nativeApis` skip set prevents `Object.defineProperty` corruption. Stays as-is.
- **`src/browser/popup.ts`** — blur-close re-enabled, debug logs removed. Stays as-is.

ECE handles frame contexts (popup pages, options pages) via its `crx-msg` IPC channel. Our SW preload handles service worker contexts via the `pane-shim` IPC channel. No conflict — different channels, same underlying TabManager.

## Chrome Proxy Invariant (Reference)

In Electron's extension contexts, `globalThis.chrome` is a V8 host-defined Proxy with non-configurable properties.

**The invariant:** `Object.defineProperty(chrome, anyProp, ...)` silently corrupts the Proxy. After any `defineProperty` call, reading ANY property from the Proxy throws:
```
TypeError: 'get' on proxy: property 'runtime' is a read-only and non-configurable
data property on the proxy target but the proxy did not return its actual value
```

**Our solution:** Never touch `chrome` directly. Instead, set `globalThis.browser` as a separate Proxy that wraps `chrome` with try/catch on every property access. Extensions that use `browser || chrome` pick up our safe wrapper.

This is why ECE's preloads are disabled — they use `Object.defineProperty(chrome, ...)` which triggers the corruption.

## IPC Flow Diagrams

### SW → Main Process (API calls)

```
Extension SW script
  → browser.alarms.create(name, info)     [extras map in browser Proxy]
  → __paneIpc.invoke("alarms", "create", name, info)
  → ipcRenderer.invoke("pane-shim", ...)  [sw-preload.js]
  → serviceWorker.ipc.handle("pane-shim") [handler.ts, registered per-SW]
  → handleAlarms(ses, "create", name, info)
```

### Main Process → SW (events)

```
alarms.ts fireAlarm()
  → ses.serviceWorkers.startWorkerForScope(scope)
  → sw.send("pane-shim-event", "alarms", "onAlarm", detail)
  → ipcRenderer.on("pane-shim-event", ...) [sw-preload.js]
  → __paneEvents("alarms", "onAlarm", detail)
  → alarmEvent._fire(detail)
  → Extension's chrome.alarms.onAlarm listeners fire
```

### Extension Opens a Window (generic)

```
Extension SW: browser.windows.create({url: 'app.html'})
  → extras.windows.create({url: 'app.html'})
  → __paneIpc.invoke("windows", "create", {url: 'app.html'})
  → handleWindows(ses, "create", {url: 'app.html'})
  → Resolve profile from session
  → Resolve full URL: chrome-extension://<id>/app.html
  → tabManager.createViewForExtension(tabId, profile, fullUrl)
  → Returns { id, focused, type } window object
```

## Extension Loading

**Phase 1 (this spec):** Extensions loaded from arbitrary paths via `loadExtension(profileId, path)`. The renderer calls `extensions:load` IPC. No bundled extensions.

**Phase 2 (future spec):** CWS download manager — downloads `.crx`, unpacks to managed directory, auto-updates. Separate feature, separate spec.

## Migration Checklist

1. Expand `sw-preload.js` with browser Proxy, process, extras map
2. Create `shims/windows.ts` and `shims/tabs.ts`
3. Update `handler.ts` dispatch with windows/tabs cases
4. Simplify `shim-preload.js` — remove `__paneFrame`, `window.close` blocking
5. Strip `extension-manager.ts` — remove all patching methods, `EXTENSIONS_DIR`, `pane-open-tab`
6. Delete `apps/desktop/extensions/` directory
7. Delete `resources/noop-preload.js`
8. Test with NordPass loaded from external path
9. Verify: SW boots, popup opens, `windows.create` opens tab, `tabs.create` works
