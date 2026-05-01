# Generic Extension Shims Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace NordPass-specific on-disk extension patching with generic preload-based shimming that works for any MV3 extension without modifying files on disk.

**Architecture:** Two preload scripts (SW + frame) registered per extension session provide all Chrome API shims. New `windows.ts` and `tabs.ts` IPC handlers enable extensions to open tabs/windows generically. `ExtensionManager` becomes a thin orchestrator with no patching code.

**Tech Stack:** Electron 41, TypeScript, electron-chrome-extensions (ECE) fork

**Spec:** `docs/superpowers/specs/2026-05-01-generic-extension-shims-design.md`

---

**Task order note:** Tasks 1-2 import `ShimDeps` from `handler.ts` (Task 3). Execute Task 3 first, or create all three together in a single commit.

---

### Task 1: Create `windows.ts` IPC handler

**Files:**
- Create: `apps/desktop/src/main/extensions/shims/windows.ts`

- [ ] **Step 1: Create windows.ts with handleWindows function**

```typescript
import { session, type Session, type BaseWindow } from "electron";

import { profileStore } from "../../../stores/profile-store";
import type { ShimDeps } from "./handler";

function resolveExtensionUrl(ses: Session, url: string): string {
  if (url.startsWith("chrome-extension://")) return url;
  const ext = ses.extensions.getAllExtensions()[0];
  if (!ext) return url;
  return `chrome-extension://${ext.id}/${url.replace(/^\//, "")}`;
}

function findProfileForSession(ses: Session): string | undefined {
  for (const profile of profileStore.getState().profiles) {
    const profileSes = (await import("electron")).session.fromPartition(
      `persist:profile-${profile.id}`,
    );
    if (profileSes === ses) return profile.id;
  }
  return undefined;
}

function makeWindowObject(mainWindow: BaseWindow): Record<string, unknown> {
  const [width, height] = mainWindow.getContentSize();
  return {
    id: 1,
    focused: mainWindow.isFocused(),
    top: 0,
    left: 0,
    width,
    height,
    type: "normal",
    state: "normal",
  };
}

export function handleWindows(
  ses: Session,
  deps: ShimDeps,
  method: string,
  ...args: unknown[]
): unknown {
  switch (method) {
    case "create": {
      const [opts] = args as [{ url?: string; type?: string }];
      const url = opts?.url ? resolveExtensionUrl(ses, opts.url) : "about:blank";
      const profileId = findProfileForSession(ses);
      if (!profileId) return makeWindowObject(deps.mainWindow);
      const profile = profileStore
        .getState()
        .profiles.find((p) => p.id === profileId);
      if (!profile) return makeWindowObject(deps.mainWindow);
      const tabId = crypto.randomUUID();
      deps.tabManager.createViewForExtension(tabId, profile, url);
      return makeWindowObject(deps.mainWindow);
    }

    case "get":
    case "getCurrent":
    case "getLastFocused":
      return makeWindowObject(deps.mainWindow);

    case "getAll":
      return [makeWindowObject(deps.mainWindow)];

    case "update":
      return makeWindowObject(deps.mainWindow);

    case "remove":
      return undefined;

    default:
      return undefined;
  }
}
```


- [ ] **Step 2: Verify TypeScript compiles**

Run: `cd apps/desktop && npx tsc --noEmit 2>&1 | grep windows`
Expected: No errors in `windows.ts`

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/src/main/extensions/shims/windows.ts
git commit -m "feat: add chrome.windows IPC handler for extension SWs"
```

---

### Task 2: Create `tabs.ts` IPC handler

**Files:**
- Create: `apps/desktop/src/main/extensions/shims/tabs.ts`

- [ ] **Step 1: Create tabs.ts with handleTabs function**

```typescript
import { session, type Session } from "electron";

import { profileStore } from "../../../stores/profile-store";
import { tabStore } from "../../../stores/tab-store";
import type { ShimDeps } from "./handler";

function resolveExtensionUrl(ses: Session, url: string): string {
  if (url.startsWith("chrome-extension://") || url.startsWith("http")) return url;
  const ext = ses.extensions.getAllExtensions()[0];
  if (!ext) return url;
  return `chrome-extension://${ext.id}/${url.replace(/^\//, "")}`;
}

function findProfileForSession(ses: Session): string | undefined {
  for (const profile of profileStore.getState().profiles) {
    if (session.fromPartition(`persist:profile-${profile.id}`) === ses)
      return profile.id;
  }
  return undefined;
}

function makeTabObject(tabId: string, url: string, active: boolean): Record<string, unknown> {
  return {
    id: tabId,
    index: 0,
    windowId: 1,
    active,
    url,
    title: "",
    status: "complete",
  };
}

export function handleTabs(
  ses: Session,
  deps: ShimDeps,
  method: string,
  ...args: unknown[]
): unknown {
  switch (method) {
    case "create": {
      const [opts] = args as [{ url?: string; active?: boolean }];
      const url = opts?.url ? resolveExtensionUrl(ses, opts.url) : "about:blank";
      const profileId = findProfileForSession(ses);
      if (!profileId) return makeTabObject("0", url, true);
      const profile = profileStore
        .getState()
        .profiles.find((p) => p.id === profileId);
      if (!profile) return makeTabObject("0", url, true);
      const tabId = crypto.randomUUID();
      deps.tabManager.createViewForExtension(tabId, profile, url);
      return makeTabObject(tabId, url, true);
    }

    case "get": {
      const [tabId] = args as [string];
      const activeTabId = tabStore.getState().activeTabId;
      return makeTabObject(
        String(tabId),
        "",
        String(tabId) === activeTabId,
      );
    }

    case "query": {
      const [filter] = args as [{ active?: boolean; currentWindow?: boolean }];
      const profiles = profileStore.getState().profiles;
      const profileId = findProfileForSession(ses);
      const profile = profiles.find((p) => p.id === profileId);
      if (!profile) return [];
      const activeTabId = tabStore.getState().activeTabId;
      let tabs = profile.tabs.map((t) =>
        makeTabObject(t.id, t.url, t.id === activeTabId),
      );
      if (filter?.active) {
        tabs = tabs.filter((t) => t.active);
      }
      return tabs;
    }

    case "update": {
      const [tabId, props] = args as [string, { url?: string; active?: boolean }];
      if (props?.active) {
        // TabManager.activate is private — use the IPC pattern
        // For now, return the tab object
      }
      return makeTabObject(String(tabId), props?.url || "", props?.active ?? false);
    }

    case "remove": {
      // Tab removal would need TabManager.destroyView which requires the tabId
      return undefined;
    }

    default:
      return undefined;
  }
}
```

- [ ] **Step 2: Verify TypeScript compiles**

Run: `cd apps/desktop && npx tsc --noEmit 2>&1 | grep tabs.ts`
Expected: No errors in `tabs.ts`

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/src/main/extensions/shims/tabs.ts
git commit -m "feat: add chrome.tabs IPC handler for extension SWs"
```

---

### Task 3: Update `handler.ts` to dispatch windows/tabs and accept deps

**Files:**
- Modify: `apps/desktop/src/main/extensions/shims/handler.ts`

- [ ] **Step 1: Add ShimDeps type and wire windows/tabs dispatch**

Replace the entire file with:

```typescript
import { ipcMain, type Session, type BaseWindow } from "electron";

import { handleAlarms } from "./alarms";
import { handleIdle } from "./idle";
import { handleWindows } from "./windows";
import { handleTabs } from "./tabs";
import type { TabManager } from "../../browser/tab-manager";

export interface ShimDeps {
  tabManager: TabManager;
  mainWindow: BaseWindow;
}

let globalRegistered = false;

export function registerShimHandler(deps: ShimDeps) {
  if (globalRegistered) return;
  globalRegistered = true;

  ipcMain.handle(
    "pane-shim",
    (event, namespace: string, method: string, ...args: unknown[]) => {
      const ses = event.sender.session;
      return dispatch(ses, deps, namespace, method, ...args);
    },
  );
}

const registeredSessions = new WeakSet<Session>();

export function registerShimHandlerForSession(ses: Session, deps: ShimDeps) {
  if (registeredSessions.has(ses)) return;
  registeredSessions.add(ses);

  const workers = new WeakSet();

  const onStatusChanged = ({
    runningStatus,
    versionId,
  }: Electron.Event<Electron.ServiceWorkersRunningStatusChangedEventParams>) => {
    if (runningStatus !== "starting") return;

    const sw = (ses as any).serviceWorkers.getWorkerFromVersionID(versionId);
    if (!sw || workers.has(sw)) return;
    if (!sw.scope?.startsWith("chrome-extension://")) return;

    workers.add(sw);
    sw.ipc.handle(
      "pane-shim",
      (_event: unknown, namespace: string, method: string, ...args: unknown[]) => {
        return dispatch(ses, deps, namespace, method, ...args);
      },
    );
  };

  ses.serviceWorkers.on("running-status-changed", onStatusChanged);
}

function dispatch(
  ses: Session,
  deps: ShimDeps,
  namespace: string,
  method: string,
  ...args: unknown[]
) {
  switch (namespace) {
    case "alarms":
      return handleAlarms(ses, method, ...args);
    case "idle":
      return handleIdle(ses, method, ...args);
    case "windows":
      return handleWindows(ses, deps, method, ...args);
    case "tabs":
      return handleTabs(ses, deps, method, ...args);
    default:
      return undefined;
  }
}
```

- [ ] **Step 2: Verify TypeScript compiles**

Run: `cd apps/desktop && npx tsc --noEmit 2>&1 | grep handler.ts`
Expected: No errors (may show errors in extension-manager.ts since it passes no deps yet — fixed in Task 5)

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/src/main/extensions/shims/handler.ts
git commit -m "feat: handler.ts dispatches windows/tabs and accepts ShimDeps"
```

---

### Task 4: Expand `sw-preload.js` with browser Proxy and extras

**Files:**
- Modify: `apps/desktop/resources/sw-preload.js`

- [ ] **Step 1: Rewrite sw-preload.js with full browser Proxy**

Replace the entire file with:

```javascript
// Service worker preload — provides the globalThis.browser Proxy and IPC bridge.
// Registered as type:"service-worker" on each extension session.
// Runs BEFORE any extension SW script.
//
// NEVER uses Object.defineProperty on globalThis.chrome — that corrupts
// the V8 Proxy invariant in Electron's extension contexts.

const { ipcRenderer } = require("electron");

// -- process global (some extensions check process.env.NODE_ENV) --
if (typeof globalThis.process === "undefined") {
  globalThis.process = { env: { NODE_ENV: "production" }, platform: "darwin", version: "" };
}

// -- IPC bridge: SW → main process --
globalThis.__paneIpc = {
  invoke(namespace, method, ...args) {
    return ipcRenderer.invoke("pane-shim", namespace, method, ...args);
  },
};

// -- Event dispatcher: main process → SW --
const eventMap = {};

globalThis.__paneEvents = function (ns, evt, data) {
  const key = ns + "." + evt;
  const listeners = eventMap[key];
  if (listeners) listeners.forEach(function (cb) { try { cb(data); } catch (e) {} });
};

ipcRenderer.on("pane-shim-event", function (_event, namespace, eventName, ...payload) {
  const dispatch = globalThis.__paneEvents;
  if (dispatch) dispatch(namespace, eventName, payload.length === 1 ? payload[0] : payload);
});

// -- Event object factory --
function makeEvent(ns, eventName) {
  const key = ns + "." + eventName;
  if (!eventMap[key]) eventMap[key] = [];
  const listeners = eventMap[key];
  return {
    addListener: function (cb) { listeners.push(cb); },
    removeListener: function (cb) {
      var i = listeners.indexOf(cb);
      if (i !== -1) listeners.splice(i, 1);
    },
    hasListener: function (cb) { return listeners.indexOf(cb) !== -1; },
    hasListeners: function () { return listeners.length > 0; },
  };
}

function noopEvent() {
  return {
    addListener: function () {},
    removeListener: function () {},
    hasListener: function () { return false; },
    hasListeners: function () { return false; },
  };
}

function noop() {}

// -- Extras: APIs not provided natively by Electron --
var ipc = globalThis.__paneIpc;

var extras = {
  action: {
    setTitle: noop, getTitle: noop, setIcon: noop, setPopup: noop, getPopup: noop,
    setBadgeText: noop, getBadgeText: noop, setBadgeBackgroundColor: noop,
    getBadgeBackgroundColor: noop, enable: noop, disable: noop, openPopup: noop,
    getUserSettings: function () { return Promise.resolve({ isOnToolbar: true }); },
    onClicked: noopEvent(), onUserSettingsChanged: noopEvent(),
  },

  browserAction: {
    setTitle: noop, getTitle: noop, setIcon: noop, setPopup: noop, getPopup: noop,
    setBadgeText: noop, getBadgeText: noop, setBadgeBackgroundColor: noop,
    getBadgeBackgroundColor: noop, enable: noop, disable: noop, openPopup: noop,
    getUserSettings: function () { return Promise.resolve({ isOnToolbar: true }); },
    onClicked: noopEvent(), onUserSettingsChanged: noopEvent(),
  },

  alarms: {
    create: function (name, info) { return ipc.invoke("alarms", "create", name, info); },
    get: function (name) { return ipc.invoke("alarms", "get", name); },
    getAll: function () { return ipc.invoke("alarms", "getAll"); },
    clear: function (name) { return ipc.invoke("alarms", "clear", name); },
    clearAll: function () { return ipc.invoke("alarms", "clearAll"); },
    onAlarm: makeEvent("alarms", "onAlarm"),
  },

  idle: {
    setDetectionInterval: function (sec) { ipc.invoke("idle", "setDetectionInterval", sec); },
    queryState: function (sec) { return ipc.invoke("idle", "queryState", sec); },
    onStateChanged: makeEvent("idle", "onStateChanged"),
  },

  windows: {
    WINDOW_ID_NONE: -1,
    WINDOW_ID_CURRENT: -2,
    create: function (opts) { return ipc.invoke("windows", "create", opts); },
    get: function (id, opts) { return ipc.invoke("windows", "get", id, opts); },
    getCurrent: function (opts) { return ipc.invoke("windows", "getCurrent", opts); },
    getLastFocused: function (opts) { return ipc.invoke("windows", "getLastFocused", opts); },
    getAll: function (opts) { return ipc.invoke("windows", "getAll", opts); },
    update: function (id, info) { return ipc.invoke("windows", "update", id, info); },
    remove: function (id) { return ipc.invoke("windows", "remove", id); },
    onCreated: noopEvent(), onRemoved: noopEvent(), onFocusChanged: noopEvent(),
  },

  tabs: {
    create: function (opts) { return ipc.invoke("tabs", "create", opts); },
    get: function (tabId) { return ipc.invoke("tabs", "get", tabId); },
    query: function (filter) { return ipc.invoke("tabs", "query", filter); },
    update: function (tabId, props) { return ipc.invoke("tabs", "update", tabId, props); },
    remove: function (tabId) { return ipc.invoke("tabs", "remove", tabId); },
    onCreated: noopEvent(), onRemoved: noopEvent(),
    onUpdated: noopEvent(), onActivated: noopEvent(),
  },

  contextMenus: {
    create: function () { return Promise.resolve(); },
    update: noop,
    remove: function () { return Promise.resolve(); },
    removeAll: function () { return Promise.resolve(); },
    onClicked: noopEvent(),
  },

  privacy: {
    network: {
      networkPredictionEnabled: { get: function () { return Promise.resolve({ value: false }); }, set: noop, clear: noop },
      webRTCIPHandlingPolicy: { get: function () { return Promise.resolve({ value: "default" }); }, set: noop, clear: noop },
    },
    services: {
      autofillAddressEnabled: { get: function () { return Promise.resolve({ value: false }); }, set: noop, clear: noop },
      autofillCreditCardEnabled: { get: function () { return Promise.resolve({ value: false }); }, set: noop, clear: noop },
      passwordSavingEnabled: { get: function () { return Promise.resolve({ value: false }); }, set: noop, clear: noop },
    },
    websites: {
      hyperlinkAuditingEnabled: { get: function () { return Promise.resolve({ value: false }); }, set: noop, clear: noop },
    },
  },
};

// -- Patches: properties merged into existing native objects --
var patches = {
  extension: { getViews: function () { return []; } },
};

// -- globalThis.browser Proxy --
var oc = globalThis.chrome;
if (oc) {
  globalThis.browser = new Proxy(oc, {
    get: function (t, k) {
      if (k in extras) return extras[k];
      var native;
      try { native = t[k]; } catch (e) { native = undefined; }
      if (k in patches && native && typeof native === "object") {
        var merged = Object.create(Object.getPrototypeOf(native));
        try {
          var names = Object.getOwnPropertyNames(native);
          for (var i = 0; i < names.length; i++) {
            try { merged[names[i]] = native[names[i]]; } catch (e) {}
          }
        } catch (e) {}
        var pk = Object.keys(patches[k]);
        for (var j = 0; j < pk.length; j++) { merged[pk[j]] = patches[k][pk[j]]; }
        return merged;
      }
      return native;
    },
    set: function (t, k, v) { try { t[k] = v; } catch (e) {} return true; },
    has: function (t, k) {
      if (k in extras) return true;
      try { return k in t; } catch (e) { return false; }
    },
    getOwnPropertyDescriptor: function (t, k) {
      if (k in extras) return { value: extras[k], writable: true, enumerable: true, configurable: true };
      try { return Object.getOwnPropertyDescriptor(t, k); } catch (e) { return undefined; }
    },
    ownKeys: function (t) {
      try {
        var k = Reflect.ownKeys(t);
        Object.keys(extras).forEach(function (ek) { if (k.indexOf(ek) === -1) k.push(ek); });
        return k;
      } catch (e) { return Object.keys(extras); }
    },
  });
}
```

- [ ] **Step 2: Verify the file has no syntax errors**

Run: `node --check apps/desktop/resources/sw-preload.js`
Expected: No output (clean parse)

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/resources/sw-preload.js
git commit -m "feat: sw-preload.js provides full browser Proxy — replaces on-disk patching"
```

---

### Task 5: Simplify `shim-preload.js`

**Files:**
- Modify: `apps/desktop/resources/shim-preload.js`

- [ ] **Step 1: Strip NordPass-specific code, keep only generic stubs**

Replace the entire file with:

```javascript
// Frame preload — patches missing APIs in extension page contexts.
// Registered as type:"frame" on each extension session.
// Uses contextBridge.executeInMainWorld to reach the main world.
// NEVER touches existing chrome Proxy properties (causes invariant violations).

const { contextBridge } = require("electron");

if ("executeInMainWorld" in contextBridge) {
  contextBridge.executeInMainWorld({
    func: function () {
      if (typeof globalThis.process === "undefined") {
        globalThis.process = { env: { NODE_ENV: "production" }, platform: "darwin", version: "" };
      }

      var chrome = globalThis.chrome;
      if (!chrome) return;

      try {
        if (chrome.extension && typeof chrome.extension.getViews !== "function") {
          chrome.extension.getViews = function () { return []; };
        }
      } catch (e) {}

      try {
        if (chrome.extension && typeof chrome.extension.isAllowedIncognitoAccess !== "function") {
          chrome.extension.isAllowedIncognitoAccess = function (cb) { if (cb) cb(false); return Promise.resolve(false); };
        }
      } catch (e) {}

      try {
        if (chrome.extension && typeof chrome.extension.isAllowedFileSchemeAccess !== "function") {
          chrome.extension.isAllowedFileSchemeAccess = function (cb) { if (cb) cb(false); return Promise.resolve(false); };
        }
      } catch (e) {}
    },
  });
}
```

- [ ] **Step 2: Verify syntax**

Run: `node --check apps/desktop/resources/shim-preload.js`
Expected: No output (clean parse)

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/resources/shim-preload.js
git commit -m "refactor: simplify shim-preload.js — remove NordPass-specific code"
```

---

### Task 6: Strip `extension-manager.ts` of all patching code

**Files:**
- Modify: `apps/desktop/src/main/extensions/extension-manager.ts`

- [ ] **Step 1: Rewrite extension-manager.ts without patching**

Replace the entire file with:

```typescript
import path from "node:path";
import {
  app,
  type BaseWindow,
  type Extension,
  ipcMain,
  session,
  type WebContents,
} from "electron";
import { ElectronChromeExtensions } from "@pane/electron-chrome-extensions";

import { profileStore } from "../../stores/profile-store";
import { extensionStore } from "../../stores/extension-store";
import type { TabManager } from "../browser/tab-manager";
import { registerShimHandler, registerShimHandlerForSession } from "./shims/handler";
import { destroyAlarms } from "./shims/alarms";
import { destroyIdle } from "./shims/idle";

const SHIM_PRELOAD_PATH = app.isPackaged
  ? path.join(process.resourcesPath, "shim-preload.js")
  : path.join(app.getAppPath(), "resources", "shim-preload.js");

const SW_PRELOAD_PATH = app.isPackaged
  ? path.join(process.resourcesPath, "sw-preload.js")
  : path.join(app.getAppPath(), "resources", "sw-preload.js");

export class ExtensionManager {
  private readonly instances = new Map<string, ElectronChromeExtensions>();
  private readonly webContentsToProfile = new WeakMap<WebContents, string>();

  constructor(
    private readonly tabManager: TabManager,
    private readonly mainWindow: BaseWindow,
  ) {
    app.setMaxListeners(0);
    registerShimHandler({ tabManager, mainWindow });
  }

  registerTab(webContents: WebContents, profileId: string) {
    this.webContentsToProfile.set(webContents, profileId);
    const ece = this.getOrCreateInstance(profileId);
    ece.addTab(webContents, this.mainWindow);
  }

  activateTab(webContents: WebContents) {
    const profileId = this.webContentsToProfile.get(webContents);
    if (!profileId) return;

    const ece = this.instances.get(profileId);
    ece?.selectTab(webContents);
  }

  unregisterTab(webContents: WebContents) {
    const profileId = this.webContentsToProfile.get(webContents);
    if (!profileId) return;

    const ece = this.instances.get(profileId);
    ece?.removeTab(webContents);
  }

  async loadExtension(
    profileId: string,
    extensionPath: string,
  ): Promise<Extension | null> {
    this.getOrCreateInstance(profileId);
    const ses = session.fromPartition(`persist:profile-${profileId}`);

    try {
      const ext = await ses.extensions.loadExtension(extensionPath);

      extensionStore.getState().addExtension(profileId, {
        id: ext.id,
        name: ext.name,
        version: ext.manifest.version,
      });

      return ext;
    } catch {
      return null;
    }
  }

  destroyProfile(profileId: string) {
    const ses = session.fromPartition(`persist:profile-${profileId}`);

    destroyAlarms(ses);
    destroyIdle(ses);

    extensionStore.getState().clearProfile(profileId);
    this.instances.delete(profileId);
  }

  registerIpc() {
    ipcMain.handle(
      "extensions:list",
      (_e: unknown, profileId: string) =>
        extensionStore.getState().extensions[profileId] ?? [],
    );

    ipcMain.handle(
      "extensions:load",
      async (_e: unknown, profileId: string, extPath: string) => {
        const ext = await this.loadExtension(profileId, extPath);

        return ext
          ? { id: ext.id, name: ext.name, version: ext.manifest.version }
          : null;
      },
    );
  }

  private getOrCreateInstance(profileId: string): ElectronChromeExtensions {
    const existing = this.instances.get(profileId);
    if (existing) return existing;

    const ses = session.fromPartition(`persist:profile-${profileId}`);

    ses.registerPreloadScript({
      id: "pane-shims-frame",
      type: "frame",
      filePath: SHIM_PRELOAD_PATH,
    });

    ses.registerPreloadScript({
      id: "pane-shims-sw",
      type: "service-worker",
      filePath: SW_PRELOAD_PATH,
    });

    registerShimHandlerForSession(ses, {
      tabManager: this.tabManager,
      mainWindow: this.mainWindow,
    });

    ElectronChromeExtensions.handleCRXProtocol(ses);

    const ece = new ElectronChromeExtensions({
      license: "GPL-3.0",
      session: ses,

      createTab: async (details) => {
        const profile = profileStore
          .getState()
          .profiles.find((p) => p.id === profileId);

        if (!profile) {
          throw new Error(`Profile ${profileId} not found`);
        }

        const tabId = crypto.randomUUID();
        const url = details.url || "about:blank";

        const view = this.tabManager.createViewForExtension(
          tabId,
          profile,
          url,
        );

        return [view.webContents, this.mainWindow];
      },

      selectTab: (webContents, _window) => {
        this.tabManager.activateByWebContents(webContents);
      },

      removeTab: (webContents, _window) => {
        this.tabManager.destroyByWebContents(webContents);
      },
    });

    this.instances.set(profileId, ece);

    return ece;
  }
}
```

- [ ] **Step 2: Update `index.ts` — pass deps to registerShimHandler**

The `index.ts` already calls `new ExtensionManager(tabManager, mainWindow)` which now passes deps internally. No changes needed to `index.ts`.

- [ ] **Step 3: Verify TypeScript compiles**

Run: `cd apps/desktop && npx tsc --noEmit 2>&1 | grep -E "extension-manager|handler|windows|tabs" | head -10`
Expected: No errors in these files (pre-existing errors in other files are OK)

- [ ] **Step 4: Commit**

```bash
git add apps/desktop/src/main/extensions/extension-manager.ts
git commit -m "refactor: strip ExtensionManager of all on-disk patching code"
```

---

### Task 7: Delete bundled extension files and unused resources

**Files:**
- Delete: `apps/desktop/extensions/eiaeiblijfjekdanodkjadfinkhbfgcd/` (entire directory)
- Delete: `apps/desktop/resources/noop-preload.js`

- [ ] **Step 1: Remove bundled NordPass extension**

```bash
rm -rf apps/desktop/extensions/eiaeiblijfjekdanodkjadfinkhbfgcd
```

If the `extensions/` directory is now empty, remove it too:

```bash
rmdir apps/desktop/extensions 2>/dev/null || true
```

- [ ] **Step 2: Remove unused noop-preload.js**

```bash
rm -f apps/desktop/resources/noop-preload.js
```

- [ ] **Step 3: Check for any remaining references to deleted files**

```bash
grep -rn "noop-preload\|EXTENSIONS_DIR\|autoLoadExtensions\|patchJsFile\|patchPopupHtml\|patchExtensionScripts\|pane-open-tab\|__paneFrame\|pane-shim\.js" apps/desktop/src/ apps/desktop/resources/ 2>/dev/null
```

Expected: No results

- [ ] **Step 4: Commit**

```bash
git add -A apps/desktop/extensions/ apps/desktop/resources/noop-preload.js
git commit -m "chore: delete bundled NordPass extension and unused noop-preload"
```

---

### Task 8: Verify type consistency across handlers

**Files:** None — verification only

- [ ] **Step 1: Verify `ShimDeps` is consistently imported**

Tasks 1 and 2 import `ShimDeps` from `handler.ts`. Task 3 defines and exports it. Verify consistency:

```bash
grep -n "ShimDeps" apps/desktop/src/main/extensions/shims/*.ts
```

Expected: `handler.ts` exports `ShimDeps`, `windows.ts` and `tabs.ts` import it.

- [ ] **Step 2: Verify no duplicate type definitions**

```bash
grep -c "interface ShimDeps" apps/desktop/src/main/extensions/shims/*.ts
```

Expected: Only 1 result (in `handler.ts`)

---

### Task 9: Build, launch, and verify end-to-end

**Files:** None — integration test

- [ ] **Step 1: Build the ECE fork** (if popup.ts was changed earlier in the session)

```bash
cd packages/electron-chrome-extensions && node esbuild.config.cjs
```

- [ ] **Step 2: Build the desktop app**

```bash
cd apps/desktop && npx electron-vite build
```

Expected: Build completes without errors

- [ ] **Step 3: Launch and verify SW boots**

```bash
cd apps/desktop && npx electron-vite dev
```

Expected output:
- No `DidStartWorkerFail` errors
- No `No handler registered for 'pane-shim'` errors
- Extension load warnings for `contextMenus`, `privacy`, `webNavigation` (expected)

- [ ] **Step 4: Test popup — click the "N" icon in the address bar**

Expected: ECE PopupView opens. If authenticated, shows NordPass vault. If unauthenticated, NordPass's own flow runs (may open `app.html` via `chrome.windows.create`).

- [ ] **Step 5: Verify no extension files were modified on disk**

```bash
# If testing with an external NordPass path, check that no files were changed
find /path/to/nordpass/extension -newer /tmp/test-marker -name "*.js" -o -name "*.html" 2>/dev/null
```

Expected: No results — extension files are untouched.

- [ ] **Step 6: Commit checkpoint**

```bash
git add -A
git commit -m "feat: generic extension shims — no on-disk patching, CWS-compatible"
```

---

### Task 10: Update documentation

**Files:**
- Modify: `apps/desktop/docs/extension-support.md`

- [ ] **Step 1: Update the architecture doc to reflect the new design**

Update `apps/desktop/docs/extension-support.md` to:
- Remove all references to on-disk patching (`patchJsFile`, `patchPopupHtml`, `pane-shim.js`)
- Remove the "How to build and test" section's file restore steps (no longer needed)
- Update the file structure diagram
- Add `windows.ts` and `tabs.ts` to the architecture
- Update the IPC flow diagrams
- Remove NordPass-specific gotchas (DESKTOP/OPEN interception, pane-shim.js, etc.)
- Update the "Remaining Work" section

- [ ] **Step 2: Commit**

```bash
git add apps/desktop/docs/extension-support.md
git commit -m "docs: update extension-support.md for generic shim architecture"
```
