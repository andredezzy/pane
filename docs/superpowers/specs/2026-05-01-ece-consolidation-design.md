# ECE Consolidation — Batteries-Included Extension Support

**Date:** 2026-05-01
**Status:** Approved
**Branch:** feat/extension-support

## Problem

Extension shim code is scattered across the desktop app (`resources/`, `src/main/extensions/shims/`) and the ECE fork (`packages/electron-chrome-extensions/`). The desktop app's `ExtensionManager` contains ~200 lines of infrastructure (preload registration, IPC handlers, blank popup detection, action.setPopup relay) that belongs inside ECE. This coupling means any Electron app using ECE would need to reimplement all of it.

## Solution

Move ALL shim code into the ECE fork. ECE becomes a batteries-included library — the consumer provides tab lifecycle callbacks via the existing constructor API, and ECE handles everything else internally: preloads, Chrome API shim handlers, IPC registration, blank popup detection, and action management.

The consumer API does not change.

## ECE File Structure

```
packages/electron-chrome-extensions/src/
├── browser/
│   ├── index.ts                    # Main class — registers preloads + IPC internally
│   ├── api/
│   │   ├── browser-action.ts       # BrowserAction (existing + onClicked emit, setPopupUrl)
│   │   ├── tabs.ts                 # Existing ECE tabs handler
│   │   ├── windows.ts              # Existing ECE windows handler
│   │   └── ...                     # Other existing API handlers
│   ├── shims/
│   │   ├── handler.ts              # IPC dispatcher (pane-shim channel, per-SW registration)
│   │   ├── alarms.ts               # chrome.alarms (setTimeout-based, per-session state)
│   │   ├── idle.ts                 # chrome.idle (powerMonitor-based)
│   │   ├── windows.ts              # chrome.windows.create → ctx.store.createTab
│   │   └── tabs.ts                 # chrome.tabs.create/query → ctx.store
│   ├── popup.ts                    # PopupView
│   ├── router.ts                   # Existing
│   └── store.ts                    # Existing
├── renderer/
│   └── index.ts                    # Existing (nativeApis skip set)
├── preloads/
│   ├── sw.js                       # Service worker preload (browser Proxy + extras + IPC)
│   └── frame.js                    # Frame preload (chrome.extension stubs + windows/tabs patches)
└── preload.ts                      # Existing entry (disabled but kept for compat)
```

## Consumer API (unchanged)

```typescript
const ece = new ElectronChromeExtensions({
  session: ses,
  createTab: async (details) => {
    const view = createMyTab(details.url);
    return [view.webContents, parentWindow];
  },
  selectTab: (wc) => activateMyTab(wc),
  removeTab: (wc) => closeMyTab(wc),
});

ece.addTab(webContents, window);
ece.selectTab(webContents);
ece.removeTab(webContents);
```

No preload paths, no IPC registration, no shim handlers. ECE handles it all.

## What ECE Does Internally

### Constructor / `prependPreload`

When an `ElectronChromeExtensions` instance is created:

1. **Registers preloads** on the session:
   - `preloads/sw.js` as `type: "service-worker"`
   - `preloads/frame.js` as `type: "frame"`
   - Resolves paths relative to ECE's build output (`__dirname`)

2. **Registers shim IPC handlers** via `handler.ts`:
   - Global `ipcMain.handle("crx-shim", ...)` for frame contexts
   - Per-SW `serviceWorker.ipc.handle("crx-shim", ...)` on `running-status-changed`
   - Dispatches to `alarms`, `idle`, `windows`, `tabs` handlers

3. **Wires shim handlers to ECE context**:
   - `shims/windows.ts` calls `ctx.store.createTab(details)` (same callback the consumer provides)
   - `shims/tabs.ts` queries `ctx.store.tabs` for tab state
   - `action.setPopup` from the SW calls `ctx.browserAction.setPopupUrl()` directly

4. **Sets up blank popup fallback**:
   - The frame preload detects empty popups after 1.5s
   - Opens the extension's full-page UI via `chrome.windows.create` (which routes to the shim handler)

5. **Sets default popup URL** when extensions load:
   - Checks for `index.html` / `popup.html` in the extension directory
   - Calls `browserAction.setPopupUrl()` if no `default_popup` in manifest

### IPC Channel

The shim IPC channel is renamed from `pane-shim` to `crx-shim` to match ECE's existing `crx-msg` naming convention. Similarly, the event channel becomes `crx-shim-event`.

### Preloads

**`preloads/sw.js`** — runs in every extension service worker:
- `contextBridge.exposeInMainWorld("__crxIpc", { invoke })` — IPC bridge
- `contextBridge.executeInMainWorld(...)` — creates:
  - `globalThis.process` polyfill
  - `globalThis.browser` Proxy wrapping `chrome` with extras map
  - `globalThis.__crxEvents` dispatcher
  - Extras: `action`, `browserAction`, `alarms`, `idle`, `windows`, `contextMenus`, `privacy`
  - Patches: `extension.getViews`

**`preloads/frame.js`** — runs in all extension pages:
- `contextBridge.exposeInMainWorld("__crxIpc", { invoke })` — IPC bridge
- `contextBridge.executeInMainWorld(...)` — patches:
  - `globalThis.process` polyfill
  - `chrome.extension.getViews`, `isAllowedIncognitoAccess`, `isAllowedFileSchemeAccess`
  - `chrome.windows.create` → IPC
  - `chrome.tabs.create` → IPC
  - `window.close()` blocking (popup pages only)
  - Blank popup detection (1.5s timeout → opens full-page fallback)

### Shim Handlers

All handlers receive `ExtensionContext` (ECE's internal context) — no external deps:

- **`shims/alarms.ts`** — `setTimeout`-based timers, per-session state, fires events via `sw.send("crx-shim-event", ...)`
- **`shims/idle.ts`** — `powerMonitor.getSystemIdleTime()`, polling, state change events
- **`shims/windows.ts`** — `create` calls `ctx.store.createTab()`, returns window objects from `ctx.store`
- **`shims/tabs.ts`** — `create` calls `ctx.store.createTab()`, `query` reads `ctx.store.tabs`
- **`shims/handler.ts`** — dispatches `crx-shim` IPC to the above + `action.setPopup`

### BrowserAction Changes

- `activateClick` skips tab requirement when popup URL is set (uses `getLastFocusedWindow()` for positioning)
- Emits `browser-action-clicked` when no popup and no active tab
- `browser-action-clicked` handler (internal) finds extension page and opens via `createTab`
- `setPopupUrl(extensionId, popup)` public method on `BrowserActionAPI`

## What Moves Out of Desktop App

### Deleted from `apps/desktop/`

```
resources/sw-preload.js              → packages/electron-chrome-extensions/src/preloads/sw.js
resources/shim-preload.js            → packages/electron-chrome-extensions/src/preloads/frame.js
src/main/extensions/shims/handler.ts → packages/electron-chrome-extensions/src/browser/shims/handler.ts
src/main/extensions/shims/alarms.ts  → packages/electron-chrome-extensions/src/browser/shims/alarms.ts
src/main/extensions/shims/idle.ts    → packages/electron-chrome-extensions/src/browser/shims/idle.ts
src/main/extensions/shims/windows.ts → packages/electron-chrome-extensions/src/browser/shims/windows.ts
src/main/extensions/shims/tabs.ts    → packages/electron-chrome-extensions/src/browser/shims/tabs.ts
```

### Simplified `ExtensionManager` (~50 lines)

```typescript
export class ExtensionManager {
  private readonly instances = new Map<string, ElectronChromeExtensions>();
  private readonly webContentsToProfile = new WeakMap<WebContents, string>();

  constructor(
    private readonly tabManager: TabManager,
    private readonly mainWindow: BaseWindow,
  ) {}

  registerTab(wc: WebContents, profileId: string) {
    this.webContentsToProfile.set(wc, profileId);
    this.getOrCreateInstance(profileId).addTab(wc, this.mainWindow);
  }

  activateTab(wc: WebContents) {
    const profileId = this.webContentsToProfile.get(wc);
    if (profileId) this.instances.get(profileId)?.selectTab(wc);
  }

  unregisterTab(wc: WebContents) {
    const profileId = this.webContentsToProfile.get(wc);
    if (profileId) this.instances.get(profileId)?.removeTab(wc);
  }

  async loadExtension(profileId: string, extensionPath: string) {
    this.getOrCreateInstance(profileId);
    const ses = session.fromPartition(`persist:profile-${profileId}`);
    return ses.extensions.loadExtension(extensionPath);
  }

  destroyProfile(profileId: string) {
    this.instances.delete(profileId);
  }

  private getOrCreateInstance(profileId: string): ElectronChromeExtensions {
    const existing = this.instances.get(profileId);
    if (existing) return existing;

    const ses = session.fromPartition(`persist:profile-${profileId}`);
    const ece = new ElectronChromeExtensions({
      license: "GPL-3.0",
      session: ses,
      createTab: async (details) => {
        const profile = profileStore.getState().profiles.find(p => p.id === profileId);
        if (!profile) throw new Error(`Profile ${profileId} not found`);
        const view = this.tabManager.createViewForExtension(
          crypto.randomUUID(), profile, details.url || "about:blank",
        );
        return [view.webContents, this.mainWindow];
      },
      selectTab: (wc) => this.tabManager.activateByWebContents(wc),
      removeTab: (wc) => this.tabManager.destroyByWebContents(wc),
    });

    this.instances.set(profileId, ece);
    return ece;
  }
}
```

No imports from `shims/`, no preload paths, no IPC registration.

## Build Changes

ECE's `esbuild.config.cjs` adds a copy step for preload files:

```javascript
// Copy preloads to dist/preloads/ (not bundled — they use require("electron"))
fs.copyFileSync('src/preloads/sw.js', 'dist/preloads/sw.js');
fs.copyFileSync('src/preloads/frame.js', 'dist/preloads/frame.js');
```

ECE resolves preload paths at runtime:
```typescript
const preloadsDir = path.join(__dirname, '..', 'preloads');
```

## Migration Checklist

1. Create `src/preloads/` directory in ECE, move `sw.js` and `frame.js` there
2. Create `src/browser/shims/` directory in ECE, move all handler files there
3. Rename IPC channels: `pane-shim` → `crx-shim`, `pane-shim-event` → `crx-shim-event`
4. Refactor shim handlers to use `ExtensionContext` instead of `ShimDeps`
5. Wire preload registration into `prependPreload` method
6. Wire shim IPC registration into constructor
7. Wire blank popup fallback and default popup URL into ECE internals
8. Update `esbuild.config.cjs` to copy preload files
9. Strip `ExtensionManager` to ~50 lines
10. Delete `apps/desktop/resources/sw-preload.js` and `shim-preload.js`
11. Delete `apps/desktop/src/main/extensions/shims/` directory
12. Test end-to-end: SW boot, popup, login flow, authenticated vault
