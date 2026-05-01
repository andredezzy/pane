# Chrome Extension Support via electron-chrome-extensions

## Problem

Pane needs full Chrome MV3 extension support, specifically for NordPass password manager. Direct Electron extension loading fails because Electron 41 doesn't implement several `chrome.*` APIs that MV3 extensions require. A previous CEF attempt proved that custom UI and full extension support are mutually exclusive in CEF without a Chromium fork.

## Solution

Integrate `electron-chrome-extensions` v4.9.0 (by Samuel Maddock) — a library that polyfills missing Chrome APIs on top of Electron's built-in extension layer. Supplement with two custom API shims for `chrome.alarms` and `chrome.idle`, which NordPass requires but the library doesn't implement.

## Key Findings

### Electron 41 compatibility

The MV3 service worker regression (GitHub issue #172) only affects Electron 40 (Chromium 144.x). Electron 41 uses Chromium 146, which includes the upstream fix. `electron-chrome-extensions` v4.9.0 works correctly on Electron 41.

### NordPass API requirements

Analysis of NordPass 7.6.20 source code reveals:

- `chrome.declarativeNetRequest` — not used (not in manifest)
- `chrome.identity` — not used (login is pure HTTPS to api.nordpass.com)
- `chrome.offscreen` — used but fully guarded with fallbacks at all call sites
- `chrome.alarms` — used unguarded at startup for auto-lock timers. Will crash without implementation.
- `chrome.idle` — used unguarded at startup for idle detection. Will crash without implementation.
- `chrome.privacy` — used unguarded at startup, but `electron-chrome-extensions` already stubs it with no-op `ChromeSetting` objects. Sufficient for NordPass.

APIs already covered by `electron-chrome-extensions`: `chrome.action`, `chrome.tabs`, `chrome.windows`, `chrome.runtime`, `chrome.storage`, `chrome.contextMenus`, `chrome.cookies`, `chrome.webNavigation`, `chrome.permissions`, `chrome.notifications`.

APIs covered by Electron natively: `chrome.scripting`.

### Multi-instance support

`electron-chrome-extensions` is designed for one instance per Electron `Session`. A module-level `WeakMap<Session, ECE>` enforces this — same session twice throws, different sessions are supported. The `RoutingDelegate` singleton dispatches IPC by session. The `<browser-action-list>` custom element supports a `partition` attribute for multi-session rendering. Per-session preload registration is clean.

The only operational concern is stacking `app.on('web-contents-created')` listeners (one per ECE instance). Node.js warns at >10 listeners — suppressed via `app.setMaxListeners(0)`.

## Architecture

### Approach: ExtensionManager with strategy-injection hooks

Two new modules that communicate via callback hooks wired in the main process orchestrator (`index.ts`). TabManager doesn't import ExtensionManager; ExtensionManager receives TabManager as a constructor dependency for the reverse flow (extension-initiated tab creation).

```
index.ts (thin orchestrator)
  ├── creates TabManager
  ├── creates ExtensionManager(tabManager, mainWindow)
  ├── wires tabManager.onTabCreated → extensionManager.registerTab
  ├── wires tabManager.onTabActivated → extensionManager.activateTab
  └── wires tabManager.onTabRemoved → extensionManager.unregisterTab
```

### Why this approach

- **Discoverability** — `ls` reveals `tab-manager.ts` and `extension-manager.ts` as separate files with clear names.
- **Single responsibility** — TabManager owns tab lifecycle, ExtensionManager owns extension lifecycle.
- **Test isolation** — TabManager tested with `onTabCreated = jest.fn()`. ExtensionManager tested with mock WebContents.
- **Open/closed** — Adding a new tab observer (analytics, session recording) is one line in index.ts.
- **Local reasoning** — TabManager fires callbacks without knowing who listens. ExtensionManager receives calls without knowing who dispatches.
- **No hidden dependencies** — ExtensionManager's dependency on TabManager is explicit via constructor injection.

## Components

### ExtensionManager (`main/extensions/extension-manager.ts`)

Owns all extension lifecycle. One instance for the entire app, managing one `electron-chrome-extensions` instance per profile session, lazily created.

```ts
class ExtensionManager {
  private instances: Map<string, ElectronChromeExtensions>

  constructor(
    private tabManager: TabManager,
    private mainWindow: BaseWindow
  )

  registerTab(webContents: WebContents, profileId: string): void
  activateTab(webContents: WebContents): void
  unregisterTab(webContents: WebContents): void

  loadExtension(profileId: string, extensionPath: string): Promise<Extension>
  unloadExtension(profileId: string, extensionId: string): void
  getExtensions(profileId: string): Extension[]

  destroyProfile(profileId: string): void

  registerIpc(): void
}
```

Lazy instantiation flow for `registerTab`:

1. Check if ECE instance exists for this profile
2. If not: get session via `session.fromPartition('persist:profile-${profileId}')`
3. Register shim IPC handlers on session
4. Register shim preload on session (must happen BEFORE ECE constructor)
5. Register CRX protocol on session
6. Create `ElectronChromeExtensions` instance with session and lifecycle callbacks
7. Auto-load extensions from the extensions directory (`apps/desktop/extensions/`). For now, all profiles load all extensions found in this directory. Per-profile extension configuration is a future concern.
8. Store instance in map
9. Call `instance.addTab(webContents, mainWindow)`

The `createTab` callback (invoked when an extension calls `chrome.tabs.create`):

1. Generate a tab ID
2. Determine profile from session → profile mapping
3. Call `tabManager.createView(tabId, profile)` with the URL from `details.url`
4. Return `[webContents, mainWindow]` to ECE

### TabManager hooks (`main/browser/tab-manager.ts`)

Three optional callback properties added to the existing class:

```ts
onTabCreated?: (webContents: WebContents, profileId: string) => void
onTabActivated?: (webContents: WebContents) => void
onTabRemoved?: (webContents: WebContents) => void
```

Fire points:
- `onTabCreated` — in `createView()`, after view is created and added to contentView
- `onTabActivated` — in `activate()`, after view is made visible
- `onTabRemoved` — in `destroyView()`, BEFORE removing from contentView (ECE needs the WebContents reference)

No other changes to TabManager.

### API Shims

#### Preload ordering constraint

ECE calls `Object.freeze(chrome)` at the end of its preload. Our shim preload must be registered BEFORE `new ElectronChromeExtensions()` (which registers ECE's preload in its constructor). Our preload runs first → writes `chrome.alarms` and `chrome.idle` → ECE runs second → freezes `chrome` including our APIs.

Shims use their own IPC channel (`pane-shim`) separate from ECE's `crx-msg`. A single `ipcMain.handle('pane-shim', ...)` handler is registered globally (once, not per-session) and dispatches to the correct shim state by looking up `event.sender.session` — the same pattern ECE's `RoutingDelegate` uses. Shim state (alarm timers, idle poll timers) is stored in a `Map<Session, ShimState>` keyed by session.

To send events back to the service worker (e.g., `alarms.onAlarm`), shims use `session.serviceWorkers.startWorkerForScope('chrome-extension://${extensionId}/')` then `sw.send('pane-shim-event', namespace, eventName, ...args)`. The shim preload registers corresponding `ipcRenderer.on('pane-shim-event', ...)` listeners to dispatch to extension callbacks.

#### Shim preload (`main/extensions/shims/preload.ts`)

Runs in both service worker and frame contexts. Defines `chrome.alarms` and `chrome.idle` on `globalThis.chrome` using `ipcRenderer.invoke('pane-shim', namespace, method, ...args)`.

Registered per-session via:
```ts
session.registerPreloadScript({ id: 'pane-shims', type: 'service-worker', filePath: SHIM_PRELOAD_PATH })
session.registerPreloadScript({ id: 'pane-shims-frame', type: 'frame', filePath: SHIM_PRELOAD_PATH })
```

#### Alarms (`main/extensions/shims/alarms.ts`)

Functional implementation using `setTimeout` in the main process.

Per-session state:
```ts
alarms: Map<string, { name: string, timer: NodeJS.Timeout, scheduledTime: number }>
```

API surface:
- `chrome.alarms.create(name, alarmInfo)` — stores setTimeout in map
- `chrome.alarms.clear(name)` — clearTimeout, remove from map
- `chrome.alarms.clearAll()` — clears all timers
- `chrome.alarms.get(name)` — returns alarm info
- `chrome.alarms.getAll()` — returns all alarms
- `chrome.alarms.onAlarm.addListener(fn)` — fired when timer expires

Each exports `register(session: Session): void`.

~80 lines.

#### Idle (`main/extensions/shims/idle.ts`)

Functional implementation using `powerMonitor.getSystemIdleTime()` polled every 15 seconds.

Per-session state:
```ts
detectionInterval: number  // seconds, default 60
lastState: 'active' | 'idle' | 'locked'
pollTimer: NodeJS.Timeout
```

API surface:
- `chrome.idle.setDetectionInterval(seconds)` — updates threshold
- `chrome.idle.queryState(seconds, cb)` — returns current state
- `chrome.idle.onStateChanged.addListener(fn)` — fired on state transitions

Each exports `register(session: Session): void`.

~60 lines.

### Extension store (`stores/extension-store.ts`)

Zustand vanilla store with sync middleware, following existing store patterns.

```ts
interface ExtensionInfo {
  id: string
  name: string
  version: string
  iconPath: string
}

interface ExtensionState {
  extensions: Record<string, ExtensionInfo[]>  // profileId → extensions
  addExtension(profileId: string, ext: ExtensionInfo): void
  removeExtension(profileId: string, extId: string): void
}
```

Synced from main → renderer via StoreSync. Not persisted to disk — extensions are re-loaded from their directories on each session creation.

### BrowserActionList component (`renderer/components/address-bar/browser-action-list.tsx`)

React wrapper around ECE's `<browser-action-list>` custom element:

```tsx
function BrowserActionList({ partition, tabId }: {
  partition: string
  tabId: string
}) {
  const ref = useRef<HTMLElement>(null)

  useEffect(() => {
    const el = ref.current
    if (!el) return
    el.setAttribute('partition', partition)
    el.setAttribute('tab', tabId)
  }, [partition, tabId])

  return <browser-action-list ref={ref} />
}
```

Mounted inside `AddressBarExtensions` in `address-bar-connected.tsx`, conditionally rendered when the active profile has extensions. The existing `AddressBarExtensions` component already returns null when it has no children.

### Preload additions (`preload/index.ts`)

1. Call `injectBrowserAction()` from `electron-chrome-extensions/dist/browser-action` to register the `<browser-action-list>` custom element
2. Add `extensions` namespace to `window.pane` with `list`, `load`, `unload` IPC methods

### Orchestrator wiring (`main/index.ts`)

Additions to existing code:

1. Create ExtensionManager with TabManager and mainWindow references
2. Wire three callback hooks: `onTabCreated`, `onTabActivated`, `onTabRemoved`
3. Call `extensionManager.registerIpc()`
4. Add extension store to StoreSync registry
5. Subscribe to profileStore to detect profile deletion → `extensionManager.destroyProfile()`

## Data Flow

### Extension icon click → popup

```
<browser-action-list> click
  → IPC 'browserAction.activate' with anchorRect
  → RoutingDelegate → correct ECE instance (by session)
  → BrowserActionAPI checks popup URL
  → PopupView created (frameless BrowserWindow, positioned below icon)
  → Loads chrome-extension://extensionId/popup.html
  → Closes on blur
```

### Extension creates a tab (OAuth flow)

```
Extension calls chrome.tabs.create({url})
  → ECE invokes createTab callback
  → ExtensionManager → tabManager.createView(tabId, profile)
  → TabManager creates WebContentsView, loads URL
  → onTabCreated fires → extensionManager.registerTab (re-entrant, safe — instance exists)
  → Returns [webContents, mainWindow] to ECE
```

### Auto-lock timer

```
NordPass calls chrome.alarms.create("autoLock", {delayInMinutes: 5})
  → Shim preload → IPC 'pane-shim' → main process alarms handler
  → setTimeout(5 * 60 * 1000)
  → Timer fires → sends 'alarms.onAlarm' to service worker
  → NordPass locks vault
```

## Edge Cases

- **Profile with no extensions:** ECE instance never created. Zero overhead.
- **Profile deleted while tabs open:** profileStore subscription detects removal → `destroyProfile()` tears down ECE instance and shim timers.
- **Multiple profiles with same extension:** Fully isolated — each session loads its own copy with separate storage and service worker.
- **Re-entrant tab registration:** When extension creates a tab → TabManager fires onTabCreated → extensionManager.registerTab checks instance exists → just calls addTab. Safe.
- **Popup positioning with sidebar:** `<browser-action-list>` is in the UI view covering the full window. `getBoundingClientRect()` returns window-relative coordinates. ECE adds parent window screen position. Popup appears below the icon correctly.
- **MaxListeners:** ExtensionManager sets `app.setMaxListeners(0)` on construction.

## Known Limitations

| Feature | Reason | Impact |
|---------|--------|--------|
| Google Sign-In (FedCM) | FedCM API not available in Electron | Users must use email/password login |
| Clipboard from service worker | `chrome.offscreen` missing | Falls back to `navigator.clipboard` (works) |
| `chrome.tabs.captureVisibleTab` | Not implemented in ECE | NordPass doesn't use it |
| Extension sync across devices | `chrome.storage.sync` falls back to local | NordPass uses local storage anyway |

## Dependencies

```
npm install electron-chrome-extensions@4.9.0
```

GPL-3.0 license — requires evaluation for Pane's licensing model. A Patron License is available via GitHub Sponsors.

## File Structure

```
apps/desktop/src/main/
├── index.ts                                          # +15 lines: wiring
├── browser/
│   └── tab-manager.ts                                # +6 lines: callback hooks
└── extensions/
    ├── extension-manager.ts                          # NEW ~200 lines
    └── shims/
        ├── preload.ts                                # NEW ~50 lines
        ├── alarms.ts                                 # NEW ~80 lines
        └── idle.ts                                   # NEW ~60 lines

apps/desktop/src/preload/
└── index.ts                                          # +3 lines: injectBrowserAction + extension IPC

apps/desktop/src/renderer/components/address-bar/
├── browser-action-list.tsx                           # NEW ~20 lines
└── address-bar-connected.tsx                         # +5 lines: mount BrowserActionList

apps/desktop/src/stores/
└── extension-store.ts                                # NEW ~30 lines
```

Total: ~440 lines new code, ~29 lines modified.
