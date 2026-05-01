# Chrome Extension Support Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Integrate `electron-chrome-extensions` into Pane so NordPass (and other MV3 extensions) load, render popups, and autofill — with per-profile isolation.

**Architecture:** An `ExtensionManager` class owns all extension lifecycle (one `electron-chrome-extensions` instance per profile session, lazily created). TabManager communicates via three optional callback hooks wired in the orchestrator (`index.ts`). Two API shims (`chrome.alarms`, `chrome.idle`) are injected via a preload that runs before the library's preload (which calls `Object.freeze(chrome)`).

**Tech Stack:** Electron 41.4.0, electron-chrome-extensions 4.9.0, Zustand 5, React 19, electron-vite

**Spec:** `docs/superpowers/specs/2026-05-01-extension-support-design.md`

---

## File Structure

```
apps/desktop/
├── electron-vite.config.ts                               # MODIFY: add shim preload entry
├── package.json                                           # MODIFY: add electron-chrome-extensions dep
├── extensions/                                            # CREATE: directory for unpacked extensions
│   └── (NordPass extension copied here)
├── src/main/
│   ├── index.ts                                           # MODIFY: wire ExtensionManager + hooks
│   ├── browser/
│   │   └── tab-manager.ts                                 # MODIFY: add 3 callback hooks
│   └── extensions/
│       ├── extension-manager.ts                           # CREATE: core extension lifecycle
│       └── shims/
│           ├── preload.ts                                 # CREATE: injects chrome.alarms + chrome.idle
│           ├── alarms.ts                                  # CREATE: main-process alarms implementation
│           └── idle.ts                                    # CREATE: main-process idle implementation
├── src/preload/
│   └── index.ts                                           # MODIFY: add injectBrowserAction + extensions IPC
├── src/stores/
│   └── extension-store.ts                                 # CREATE: extension state per profile
├── src/renderer/
│   ├── main.tsx                                           # MODIFY: extend Window type
│   └── components/address-bar/
│       ├── browser-action-list.tsx                        # CREATE: React wrapper for <browser-action-list>
│       └── address-bar-connected.tsx                      # MODIFY: mount BrowserActionList
```

---

## Task 1: Install dependency and set up extension directory

**Files:**
- Modify: `apps/desktop/package.json`

- [ ] **Step 1: Install electron-chrome-extensions**

```bash
cd /Users/andrevictor/www/pane/apps/desktop && npm install electron-chrome-extensions@4.9.0
```

- [ ] **Step 2: Copy NordPass extension to desktop app**

The NordPass extension is at `apps/browser/extensions/eiaeiblijfjekdanodkjadfinkhbfgcd/`. Copy it to `apps/desktop/extensions/` so the desktop app has access at runtime.

```bash
mkdir -p /Users/andrevictor/www/pane/apps/desktop/extensions
cp -r /Users/andrevictor/www/pane/apps/browser/extensions/eiaeiblijfjekdanodkjadfinkhbfgcd /Users/andrevictor/www/pane/apps/desktop/extensions/
```

- [ ] **Step 3: Verify the extension manifest exists**

```bash
cat /Users/andrevictor/www/pane/apps/desktop/extensions/eiaeiblijfjekdanodkjadfinkhbfgcd/manifest.json | head -5
```

Expected: JSON with `"manifest_version": 3` and `"name": "NordPass"`.

- [ ] **Step 4: Commit**

```bash
cd /Users/andrevictor/www/pane
git add apps/desktop/package.json apps/desktop/package-lock.json apps/desktop/extensions/
git commit -m "$(cat <<'EOF'
feat: add electron-chrome-extensions and NordPass extension files
EOF
)"
```

---

## Task 2: Add TabManager callback hooks

**Files:**
- Modify: `apps/desktop/src/main/browser/tab-manager.ts`

- [ ] **Step 1: Add callback hook properties to the TabManager class**

In `apps/desktop/src/main/browser/tab-manager.ts`, add three optional callback properties to the class. Insert them right after the `private window` property (after line 28):

```ts
// In the TabManager class, after `private window: BaseWindow | null = null;`
onTabCreated?: (webContents: WebContents, profileId: string) => void;
onTabActivated?: (webContents: WebContents) => void;
onTabRemoved?: (webContents: WebContents) => void;
```

The `WebContents` type is already imported from `electron` on line 1.

- [ ] **Step 2: Fire onTabCreated in the tabs:open IPC handler**

In `registerIpc()`, the `tabs:open` handler (line 50–73) creates a view, adds it, and loads a URL. Add the `onTabCreated` callback fire after the view is added to the window and URL loaded (after line 69 `view.webContents.loadURL(targetUrl)`):

```ts
this.onTabCreated?.(view.webContents, profileId);
```

- [ ] **Step 3: Fire onTabCreated in restore()**

In `restore()` (line 34–47), after `view.webContents.loadURL(...)` on line 44:

```ts
this.onTabCreated?.(view.webContents, profile.id);
```

- [ ] **Step 4: Fire onTabActivated in activate()**

In the private `activate()` method (line 146–155), after `tabStore.getState().setActiveTab(tabId)` on line 154, add:

```ts
const activatedView = this.views.get(tabId);
if (activatedView) {
  this.onTabActivated?.(activatedView.webContents);
}
```

- [ ] **Step 5: Fire onTabRemoved in destroyView()**

In `destroyView()` (line 163–173), BEFORE the view is removed from the window (before line 170 `this.window?.contentView.removeChildView(view)`), add:

```ts
this.onTabRemoved?.(view.webContents);
```

This must fire before `removeChildView` and `close()` because `electron-chrome-extensions` needs the WebContents reference to unregister it.

- [ ] **Step 6: Verify the app still builds**

```bash
cd /Users/andrevictor/www/pane/apps/desktop && npm run build
```

Expected: build succeeds with no errors.

- [ ] **Step 7: Commit**

```bash
cd /Users/andrevictor/www/pane
git add apps/desktop/src/main/browser/tab-manager.ts
git commit -m "$(cat <<'EOF'
feat: add lifecycle callback hooks to TabManager
EOF
)"
```

---

## Task 3: Create the alarms shim (main process)

**Files:**
- Create: `apps/desktop/src/main/extensions/shims/alarms.ts`

- [ ] **Step 1: Create the shims directory**

```bash
mkdir -p /Users/andrevictor/www/pane/apps/desktop/src/main/extensions/shims
```

- [ ] **Step 2: Write the alarms shim**

Create `apps/desktop/src/main/extensions/shims/alarms.ts`:

```ts
import type { Session } from "electron";

interface AlarmEntry {
  name: string;
  timer: ReturnType<typeof setTimeout>;
  scheduledTime: number;
  periodInMinutes?: number;
}

interface AlarmsState {
  alarms: Map<string, AlarmEntry>;
  listeners: Array<(alarm: { name: string; scheduledTime: number }) => void>;
}

const sessionState = new Map<Session, AlarmsState>();

function getState(ses: Session): AlarmsState {
  let state = sessionState.get(ses);

  if (!state) {
    state = { alarms: new Map(), listeners: [] };
    sessionState.set(ses, state);
  }

  return state;
}

function fireAlarm(ses: Session, name: string) {
  const state = getState(ses);
  const entry = state.alarms.get(name);

  if (!entry) {
    return;
  }

  const detail = { name, scheduledTime: entry.scheduledTime };

  for (const scope of getExtensionScopes(ses)) {
    ses.serviceWorkers
      .startWorkerForScope(scope)
      .then((sw) => sw.send("pane-shim-event", "alarms", "onAlarm", detail))
      .catch(() => {});
  }

  if (entry.periodInMinutes) {
    const delayMs = entry.periodInMinutes * 60_000;
    entry.scheduledTime = Date.now() + delayMs;
    entry.timer = setTimeout(() => fireAlarm(ses, name), delayMs);
  } else {
    state.alarms.delete(name);
  }
}

function getExtensionScopes(ses: Session): string[] {
  return ses.getAllExtensions().map((ext) => `chrome-extension://${ext.id}/`);
}

export function handleAlarms(
  ses: Session,
  method: string,
  ...args: unknown[]
): unknown {
  const state = getState(ses);

  switch (method) {
    case "create": {
      const [name, info] = args as [
        string,
        { delayInMinutes?: number; periodInMinutes?: number; when?: number },
      ];

      const existing = state.alarms.get(name);

      if (existing) {
        clearTimeout(existing.timer);
      }

      let delayMs: number;

      if (info.when) {
        delayMs = Math.max(0, info.when - Date.now());
      } else if (info.delayInMinutes) {
        delayMs = info.delayInMinutes * 60_000;
      } else if (info.periodInMinutes) {
        delayMs = info.periodInMinutes * 60_000;
      } else {
        delayMs = 0;
      }

      const entry: AlarmEntry = {
        name,
        scheduledTime: Date.now() + delayMs,
        periodInMinutes: info.periodInMinutes,
        timer: setTimeout(() => fireAlarm(ses, name), delayMs),
      };

      state.alarms.set(name, entry);

      return undefined;
    }

    case "get": {
      const [name] = args as [string];
      const entry = state.alarms.get(name);

      if (!entry) {
        return undefined;
      }

      return {
        name: entry.name,
        scheduledTime: entry.scheduledTime,
        periodInMinutes: entry.periodInMinutes,
      };
    }

    case "getAll": {
      return [...state.alarms.values()].map((e) => ({
        name: e.name,
        scheduledTime: e.scheduledTime,
        periodInMinutes: e.periodInMinutes,
      }));
    }

    case "clear": {
      const [name] = args as [string];
      const entry = state.alarms.get(name);

      if (entry) {
        clearTimeout(entry.timer);
        state.alarms.delete(name);
      }

      return true;
    }

    case "clearAll": {
      for (const entry of state.alarms.values()) {
        clearTimeout(entry.timer);
      }

      state.alarms.clear();

      return true;
    }

    default:
      return undefined;
  }
}

export function destroyAlarms(ses: Session) {
  const state = sessionState.get(ses);

  if (!state) {
    return;
  }

  for (const entry of state.alarms.values()) {
    clearTimeout(entry.timer);
  }

  sessionState.delete(ses);
}
```

- [ ] **Step 3: Verify it compiles**

```bash
cd /Users/andrevictor/www/pane/apps/desktop && npx tsc --noEmit
```

Expected: no errors.

- [ ] **Step 4: Commit**

```bash
cd /Users/andrevictor/www/pane
git add apps/desktop/src/main/extensions/shims/alarms.ts
git commit -m "$(cat <<'EOF'
feat: add chrome.alarms shim for main process
EOF
)"
```

---

## Task 4: Create the idle shim (main process)

**Files:**
- Create: `apps/desktop/src/main/extensions/shims/idle.ts`

- [ ] **Step 1: Write the idle shim**

Create `apps/desktop/src/main/extensions/shims/idle.ts`:

```ts
import { powerMonitor, type Session } from "electron";

type IdleState = "active" | "idle" | "locked";

const POLL_INTERVAL_MS = 15_000;

interface IdleSessionState {
  detectionInterval: number;
  lastState: IdleState;
  pollTimer: ReturnType<typeof setInterval> | null;
}

const sessionState = new Map<Session, IdleSessionState>();

function getState(ses: Session): IdleSessionState {
  let state = sessionState.get(ses);

  if (!state) {
    state = {
      detectionInterval: 60,
      lastState: "active",
      pollTimer: null,
    };

    sessionState.set(ses, state);
  }

  return state;
}

function getCurrentState(detectionInterval: number): IdleState {
  const idleTime = powerMonitor.getSystemIdleTime();

  return idleTime >= detectionInterval ? "idle" : "active";
}

function startPolling(ses: Session) {
  const state = getState(ses);

  if (state.pollTimer) {
    return;
  }

  state.pollTimer = setInterval(() => {
    const newState = getCurrentState(state.detectionInterval);

    if (newState !== state.lastState) {
      state.lastState = newState;

      for (const ext of ses.getAllExtensions()) {
        const scope = `chrome-extension://${ext.id}/`;

        ses.serviceWorkers
          .startWorkerForScope(scope)
          .then((sw) =>
            sw.send("pane-shim-event", "idle", "onStateChanged", newState),
          )
          .catch(() => {});
      }
    }
  }, POLL_INTERVAL_MS);
}

export function handleIdle(
  ses: Session,
  method: string,
  ...args: unknown[]
): unknown {
  const state = getState(ses);

  switch (method) {
    case "setDetectionInterval": {
      const [intervalSec] = args as [number];
      state.detectionInterval = intervalSec;
      startPolling(ses);

      return undefined;
    }

    case "queryState": {
      const [detectionIntervalSec] = args as [number];

      return getCurrentState(detectionIntervalSec);
    }

    default:
      return undefined;
  }
}

export function destroyIdle(ses: Session) {
  const state = sessionState.get(ses);

  if (state?.pollTimer) {
    clearInterval(state.pollTimer);
  }

  sessionState.delete(ses);
}
```

- [ ] **Step 2: Verify it compiles**

```bash
cd /Users/andrevictor/www/pane/apps/desktop && npx tsc --noEmit
```

Expected: no errors.

- [ ] **Step 3: Commit**

```bash
cd /Users/andrevictor/www/pane
git add apps/desktop/src/main/extensions/shims/idle.ts
git commit -m "$(cat <<'EOF'
feat: add chrome.idle shim for main process
EOF
)"
```

---

## Task 5: Create the shim preload

The shim preload runs in service worker and frame contexts BEFORE `electron-chrome-extensions`'s preload. It defines `chrome.alarms` and `chrome.idle` on `globalThis.chrome` so they survive ECE's `Object.freeze(chrome)`.

**Files:**
- Create: `apps/desktop/src/main/extensions/shims/preload.ts`
- Modify: `apps/desktop/electron-vite.config.ts`

- [ ] **Step 1: Write the shim preload**

Create `apps/desktop/src/main/extensions/shims/preload.ts`:

```ts
const { ipcRenderer } = require("electron");

function setupShims() {
  const chrome = (globalThis as any).chrome || ((globalThis as any).chrome = {});

  const invoke = (namespace: string, method: string, ...args: unknown[]) =>
    ipcRenderer.invoke("pane-shim", namespace, method, ...args);

  const eventListeners: Record<string, Array<(...args: unknown[]) => void>> = {};

  ipcRenderer.on(
    "pane-shim-event",
    (_event: unknown, namespace: string, eventName: string, ...args: unknown[]) => {
      const key = `${namespace}.${eventName}`;
      const listeners = eventListeners[key];

      if (listeners) {
        for (const listener of listeners) {
          try {
            listener(...args);
          } catch {}
        }
      }
    },
  );

  function makeEventTarget(namespace: string, eventName: string) {
    const key = `${namespace}.${eventName}`;

    return {
      addListener(fn: (...args: unknown[]) => void) {
        if (!eventListeners[key]) {
          eventListeners[key] = [];
        }

        eventListeners[key].push(fn);
      },
      removeListener(fn: (...args: unknown[]) => void) {
        const list = eventListeners[key];

        if (list) {
          eventListeners[key] = list.filter((l) => l !== fn);
        }
      },
      hasListener(fn: (...args: unknown[]) => void) {
        return eventListeners[key]?.includes(fn) ?? false;
      },
      hasListeners() {
        return (eventListeners[key]?.length ?? 0) > 0;
      },
    };
  }

  chrome.alarms = {
    create: (name: string, alarmInfo: unknown) =>
      invoke("alarms", "create", name, alarmInfo),
    get: (name: string) => invoke("alarms", "get", name),
    getAll: () => invoke("alarms", "getAll"),
    clear: (name: string) => invoke("alarms", "clear", name),
    clearAll: () => invoke("alarms", "clearAll"),
    onAlarm: makeEventTarget("alarms", "onAlarm"),
  };

  chrome.idle = {
    setDetectionInterval: (intervalInSeconds: number) =>
      invoke("idle", "setDetectionInterval", intervalInSeconds),
    queryState: (detectionIntervalInSeconds: number) =>
      invoke("idle", "queryState", detectionIntervalInSeconds),
    onStateChanged: makeEventTarget("idle", "onStateChanged"),
    IdleState: { ACTIVE: "active", IDLE: "idle", LOCKED: "locked" },
  };
}

const processType =
  typeof process !== "undefined" ? (process as any).type : undefined;
const locationHref =
  typeof location !== "undefined" ? location.href : "";

if (
  processType === "service-worker" ||
  locationHref.startsWith("chrome-extension://")
) {
  setupShims();
}
```

- [ ] **Step 2: Add shim preload as a separate entry in electron-vite config**

The shim preload needs to be built as a separate file. Modify `apps/desktop/electron-vite.config.ts`:

```ts
import path from "node:path";
import react from "@vitejs/plugin-react";
import { defineConfig, externalizeDepsPlugin } from "electron-vite";

export default defineConfig({
  main: {
    plugins: [externalizeDepsPlugin()],
  },
  preload: {
    plugins: [externalizeDepsPlugin()],
    build: {
      rollupOptions: {
        input: {
          index: path.join(__dirname, "src/preload/index.ts"),
          "shim-preload": path.join(
            __dirname,
            "src/main/extensions/shims/preload.ts",
          ),
        },
      },
    },
  },
  renderer: {
    plugins: [react()],
  },
});
```

This produces `out/preload/index.mjs` (existing) and `out/preload/shim-preload.mjs` (new).

- [ ] **Step 3: Verify it builds**

```bash
cd /Users/andrevictor/www/pane/apps/desktop && npm run build
```

Expected: build succeeds. Check output:

```bash
ls out/preload/
```

Expected: `index.mjs` and `shim-preload.mjs` both exist.

- [ ] **Step 4: Commit**

```bash
cd /Users/andrevictor/www/pane
git add apps/desktop/src/main/extensions/shims/preload.ts apps/desktop/electron-vite.config.ts
git commit -m "$(cat <<'EOF'
feat: add shim preload for chrome.alarms and chrome.idle
EOF
)"
```

---

## Task 6: Register the global shim IPC handler

The shims need a single global `ipcMain.handle('pane-shim')` handler that dispatches by session. This is registered once at startup, not per-profile.

**Files:**
- Create: `apps/desktop/src/main/extensions/shims/handler.ts`

- [ ] **Step 1: Write the global shim IPC handler**

Create `apps/desktop/src/main/extensions/shims/handler.ts`:

```ts
import { ipcMain } from "electron";

import { handleAlarms } from "./alarms";
import { handleIdle } from "./idle";

let registered = false;

export function registerShimHandler() {
  if (registered) {
    return;
  }

  registered = true;

  ipcMain.handle(
    "pane-shim",
    (event, namespace: string, method: string, ...args: unknown[]) => {
      const ses = event.sender.session;

      switch (namespace) {
        case "alarms":
          return handleAlarms(ses, method, ...args);
        case "idle":
          return handleIdle(ses, method, ...args);
        default:
          return undefined;
      }
    },
  );
}
```

- [ ] **Step 2: Verify it compiles**

```bash
cd /Users/andrevictor/www/pane/apps/desktop && npx tsc --noEmit
```

- [ ] **Step 3: Commit**

```bash
cd /Users/andrevictor/www/pane
git add apps/desktop/src/main/extensions/shims/handler.ts
git commit -m "$(cat <<'EOF'
feat: add global IPC handler for API shims
EOF
)"
```

---

## Task 7: Create the extension store

**Files:**
- Create: `apps/desktop/src/stores/extension-store.ts`

- [ ] **Step 1: Write the extension store**

Create `apps/desktop/src/stores/extension-store.ts` following the exact same pattern as `tab-store.ts`:

```ts
import { createStore } from "zustand/vanilla";

import { sync } from "./middlewares/sync";

export interface ExtensionInfo {
  id: string;
  name: string;
  version: string;
}

interface ExtensionState {
  extensions: Record<string, ExtensionInfo[]>;

  addExtension: (profileId: string, ext: ExtensionInfo) => void;
  removeExtension: (profileId: string, extensionId: string) => void;
  clearProfile: (profileId: string) => void;
}

export const extensionStore = createStore<ExtensionState>()(
  sync(
    (set) => ({
      extensions: {},

      addExtension: (profileId, ext) =>
        set((s) => ({
          extensions: {
            ...s.extensions,
            [profileId]: [...(s.extensions[profileId] ?? []), ext],
          },
        })),

      removeExtension: (profileId, extensionId) =>
        set((s) => ({
          extensions: {
            ...s.extensions,
            [profileId]: (s.extensions[profileId] ?? []).filter(
              (e) => e.id !== extensionId,
            ),
          },
        })),

      clearProfile: (profileId) =>
        set((s) => {
          const { [profileId]: _, ...rest } = s.extensions;

          return { extensions: rest };
        }),
    }),
    { name: "extension-store" },
  ),
);
```

- [ ] **Step 2: Verify it compiles**

```bash
cd /Users/andrevictor/www/pane/apps/desktop && npx tsc --noEmit
```

- [ ] **Step 3: Commit**

```bash
cd /Users/andrevictor/www/pane
git add apps/desktop/src/stores/extension-store.ts
git commit -m "$(cat <<'EOF'
feat: add extension store for per-profile extension state
EOF
)"
```

---

## Task 8: Create the ExtensionManager

**Files:**
- Create: `apps/desktop/src/main/extensions/extension-manager.ts`

- [ ] **Step 1: Write the ExtensionManager class**

Create `apps/desktop/src/main/extensions/extension-manager.ts`:

```ts
import path from "node:path";
import fs from "node:fs";
import {
  app,
  type BaseWindow,
  type Extension,
  session,
  type WebContents,
} from "electron";
import { ElectronChromeExtensions } from "electron-chrome-extensions";

import { profileStore } from "../../stores/profile-store";
import { extensionStore } from "../../stores/extension-store";
import type { TabManager } from "../browser/tab-manager";
import { registerShimHandler } from "./shims/handler";
import { destroyAlarms } from "./shims/alarms";
import { destroyIdle } from "./shims/idle";

const SHIM_PRELOAD_PATH = path.join(
  __dirname,
  "../../preload/shim-preload.mjs",
);

const EXTENSIONS_DIR = path.join(app.getAppPath(), "extensions");

export class ExtensionManager {
  private readonly instances = new Map<string, ElectronChromeExtensions>();
  private readonly webContentsToProfile = new WeakMap<WebContents, string>();

  constructor(
    private readonly tabManager: TabManager,
    private readonly mainWindow: BaseWindow,
  ) {
    app.setMaxListeners(0);
    registerShimHandler();
  }

  registerTab(webContents: WebContents, profileId: string) {
    this.webContentsToProfile.set(webContents, profileId);
    const ece = this.getOrCreateInstance(profileId);
    ece.addTab(webContents, this.mainWindow);
  }

  activateTab(webContents: WebContents) {
    const profileId = this.webContentsToProfile.get(webContents);

    if (!profileId) {
      return;
    }

    const ece = this.instances.get(profileId);
    ece?.selectTab(webContents);
  }

  unregisterTab(webContents: WebContents) {
    const profileId = this.webContentsToProfile.get(webContents);

    if (!profileId) {
      return;
    }

    const ece = this.instances.get(profileId);
    ece?.removeTab(webContents);
  }

  async loadExtension(
    profileId: string,
    extensionPath: string,
  ): Promise<Extension | null> {
    const ses = session.fromPartition(`persist:profile-${profileId}`);

    try {
      const ext = await ses.loadExtension(extensionPath);

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
    const { ipcMain } = require("electron");

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

    if (existing) {
      return existing;
    }

    const ses = session.fromPartition(`persist:profile-${profileId}`);

    ses.registerPreloadScript({
      id: "pane-shims",
      type: "service-worker",
      filePath: SHIM_PRELOAD_PATH,
    });

    ses.registerPreloadScript({
      id: "pane-shims-frame",
      type: "frame",
      filePath: SHIM_PRELOAD_PATH,
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

      selectTab: (webContents) => {
        this.tabManager.activateByWebContents(webContents);
      },

      removeTab: (webContents) => {
        this.tabManager.destroyByWebContents(webContents);
      },
    });

    this.instances.set(profileId, ece);

    this.autoLoadExtensions(profileId);

    return ece;
  }

  private autoLoadExtensions(profileId: string) {
    if (!fs.existsSync(EXTENSIONS_DIR)) {
      return;
    }

    const entries = fs.readdirSync(EXTENSIONS_DIR, { withFileTypes: true });

    for (const entry of entries) {
      if (entry.isDirectory()) {
        const extPath = path.join(EXTENSIONS_DIR, entry.name);
        const manifestPath = path.join(extPath, "manifest.json");

        if (fs.existsSync(manifestPath)) {
          this.loadExtension(profileId, extPath).catch(() => {});
        }
      }
    }
  }
}
```

- [ ] **Step 2: Add helper methods to TabManager**

The ExtensionManager's ECE callbacks need to create/activate/destroy tabs by WebContents. Add three public helper methods to `apps/desktop/src/main/browser/tab-manager.ts`.

Add at the end of the class (before the `private getContentBounds()` method):

```ts
createViewForExtension(
  tabId: string,
  profile: BrowserProfile,
  url: string,
): WebContentsView {
  if (!this.window) {
    throw new Error("Window not attached");
  }

  const view = this.createView(tabId, profile);
  this.views.set(tabId, view);
  this.window.contentView.addChildView(view);
  view.webContents.loadURL(url);

  profileStore.getState().openTab(profile.id, tabId, url);
  this.activate(tabId);

  return view;
}

activateByWebContents(webContents: WebContents) {
  for (const [tabId, view] of this.views) {
    if (view.webContents === webContents) {
      this.activate(tabId);

      return;
    }
  }
}

destroyByWebContents(webContents: WebContents) {
  for (const [tabId, view] of this.views) {
    if (view.webContents === webContents) {
      this.destroyView(tabId);
      profileStore.getState().closeTab(tabId);

      return;
    }
  }
}
```

- [ ] **Step 3: Verify it compiles**

```bash
cd /Users/andrevictor/www/pane/apps/desktop && npx tsc --noEmit
```

Expected: no errors. If `electron-chrome-extensions` types are not found, check that the package was installed correctly in Task 1.

- [ ] **Step 4: Commit**

```bash
cd /Users/andrevictor/www/pane
git add apps/desktop/src/main/extensions/extension-manager.ts apps/desktop/src/main/browser/tab-manager.ts
git commit -m "$(cat <<'EOF'
feat: add ExtensionManager with lazy per-profile ECE instances
EOF
)"
```

---

## Task 9: Update the preload with extension support

**Files:**
- Modify: `apps/desktop/src/preload/index.ts`

- [ ] **Step 1: Add injectBrowserAction and extension IPC**

Update `apps/desktop/src/preload/index.ts`. Add the `injectBrowserAction` import and call at the top, and add the `extensions` namespace to the `pane` object:

Replace the entire file with:

```ts
import { contextBridge, ipcRenderer } from "electron";
import { injectBrowserAction } from "electron-chrome-extensions/dist/browser-action";

injectBrowserAction();

const electronSync = {
  send: (storeName: string, state: string) => {
    ipcRenderer.send("sync:push", { store: storeName, state });
  },

  onReceive: (callback: (storeName: string, state: string) => void) => {
    ipcRenderer.on(
      "sync:push",
      (_event, data: { store: string; state: string }) => {
        callback(data.store, data.state);
      },
    );
  },

  requestState: (storeName: string): Promise<string | null> => {
    return ipcRenderer.invoke("sync:get", storeName);
  },
};

const pane = {
  tabs: {
    open: (profileId: string, url?: string) =>
      ipcRenderer.invoke("tabs:open", profileId, url),
    close: (tabId: string) => ipcRenderer.invoke("tabs:close", tabId),
    switch: (tabId: string) => ipcRenderer.invoke("tabs:switch", tabId),
    navigate: (url: string) => ipcRenderer.invoke("tabs:navigate", url),
    goBack: () => ipcRenderer.invoke("tabs:go-back"),
    goForward: () => ipcRenderer.invoke("tabs:go-forward"),
    reload: () => ipcRenderer.invoke("tabs:reload"),
    hideAll: () => ipcRenderer.invoke("tabs:hide-all"),
    showActive: () => ipcRenderer.invoke("tabs:show-active"),
  },
  settings: {
    detectBrowser: () => ipcRenderer.invoke("settings:detect-browser"),
  },
  extensions: {
    list: (profileId: string) =>
      ipcRenderer.invoke("extensions:list", profileId),
    load: (profileId: string, path: string) =>
      ipcRenderer.invoke("extensions:load", profileId, path),
  },
};

contextBridge.exposeInMainWorld("electronSync", electronSync);
contextBridge.exposeInMainWorld("pane", pane);

export type ElectronSync = typeof electronSync;
export type PaneAPI = typeof pane;
```

- [ ] **Step 2: Verify the import path exists**

The `injectBrowserAction` export path may differ. Check:

```bash
ls /Users/andrevictor/www/pane/apps/desktop/node_modules/electron-chrome-extensions/dist/ | grep browser
```

If the path is different (e.g., `dist/esm/browser-action` or just `electron-chrome-extensions`), update the import accordingly.

- [ ] **Step 3: Verify it builds**

```bash
cd /Users/andrevictor/www/pane/apps/desktop && npm run build
```

Expected: build succeeds.

- [ ] **Step 4: Commit**

```bash
cd /Users/andrevictor/www/pane
git add apps/desktop/src/preload/index.ts
git commit -m "$(cat <<'EOF'
feat: add injectBrowserAction and extension IPC to preload
EOF
)"
```

---

## Task 10: Create the BrowserActionList React component

**Files:**
- Create: `apps/desktop/src/renderer/components/address-bar/browser-action-list.tsx`

- [ ] **Step 1: Write the BrowserActionList component**

Create `apps/desktop/src/renderer/components/address-bar/browser-action-list.tsx`:

```tsx
import { useEffect, useRef } from "react";

declare global {
  // eslint-disable-next-line @typescript-eslint/no-namespace
  namespace JSX {
    interface IntrinsicElements {
      "browser-action-list": React.DetailedHTMLProps<
        React.HTMLAttributes<HTMLElement> & {
          partition?: string;
          tab?: string;
        },
        HTMLElement
      >;
    }
  }
}

export function BrowserActionList({
  partition,
  tabId,
}: {
  partition: string;
  tabId: string;
}) {
  const ref = useRef<HTMLElement>(null);

  useEffect(() => {
    const el = ref.current;

    if (!el) {
      return;
    }

    el.setAttribute("partition", partition);
    el.setAttribute("tab", tabId);
  }, [partition, tabId]);

  return <browser-action-list ref={ref} />;
}
```

- [ ] **Step 2: Verify it compiles**

```bash
cd /Users/andrevictor/www/pane/apps/desktop && npx tsc --noEmit
```

- [ ] **Step 3: Commit**

```bash
cd /Users/andrevictor/www/pane
git add apps/desktop/src/renderer/components/address-bar/browser-action-list.tsx
git commit -m "$(cat <<'EOF'
feat: add BrowserActionList React wrapper component
EOF
)"
```

---

## Task 11: Mount BrowserActionList in the address bar

**Files:**
- Modify: `apps/desktop/src/renderer/components/address-bar/address-bar-connected.tsx`

- [ ] **Step 1: Update BrowserAddressBar to render extension icons**

Update `apps/desktop/src/renderer/components/address-bar/address-bar-connected.tsx`. Add the import for `BrowserActionList` and `extensionStore`, then conditionally render it inside `AddressBarExtensions`.

Replace the entire file with:

```tsx
import { useState } from "react";
import { useStore } from "zustand/react";

import { extensionStore } from "../../../stores/extension-store";
import {
  ProfileColor,
  type ProfileColor as ProfileColorType,
  profileStore,
} from "../../../stores/profile-store";
import { tabStore } from "../../../stores/tab-store";
import {
  AddressBar,
  AddressBarExtensions,
  AddressBarInput,
  AddressBarNav,
  AddressBarProfileBadge,
} from "./address-bar";
import { BrowserActionList } from "./browser-action-list";

export function BrowserAddressBar() {
  const activeTabId = useStore(tabStore, (s) => s.activeTabId);
  const profiles = useStore(profileStore, (s) => s.profiles);
  const extensions = useStore(extensionStore, (s) => s.extensions);

  let activeUrl = "";
  let profileName = "";
  let profileColor: ProfileColorType = ProfileColor.BLUE;
  let activeProfileId = "";

  for (const profile of profiles) {
    const tab = profile.tabs.find((t) => t.id === activeTabId);

    if (tab) {
      activeUrl = tab.url;
      profileName = profile.name;
      profileColor = profile.color;
      activeProfileId = profile.id;
      break;
    }
  }

  const [inputValue, setInputValue] = useState("");
  const [isFocused, setIsFocused] = useState(false);

  const displayUrl = isFocused ? inputValue : activeUrl;

  const handleSubmit = (e: React.FormEvent) => {
    e.preventDefault();

    if (inputValue.trim()) {
      window.pane.tabs.navigate(inputValue.trim());
      (document.activeElement as HTMLElement)?.blur();
    }
  };

  if (!activeTabId) {
    return null;
  }

  const profileExtensions = extensions[activeProfileId];

  return (
    <AddressBar>
      <AddressBarNav
        onBack={() => window.pane.tabs.goBack()}
        onForward={() => window.pane.tabs.goForward()}
        onReload={() => window.pane.tabs.reload()}
      />

      <form onSubmit={handleSubmit} className="flex flex-1">
        <AddressBarInput
          value={displayUrl}
          onChange={(e) => setInputValue(e.target.value)}
          onFocus={() => {
            setInputValue(activeUrl);
            setIsFocused(true);
          }}
          onBlur={() => setIsFocused(false)}
          placeholder="Search or enter URL"
        />
      </form>

      <AddressBarExtensions>
        {profileExtensions?.length > 0 && (
          <BrowserActionList
            partition={`persist:profile-${activeProfileId}`}
            tabId={activeTabId}
          />
        )}
      </AddressBarExtensions>

      {profileName ? (
        <AddressBarProfileBadge color={profileColor}>
          {profileName}
        </AddressBarProfileBadge>
      ) : null}
    </AddressBar>
  );
}
```

Changes from the original:
- Added `extensionStore` import and `BrowserActionList` import
- Added `extensions` subscription from `extensionStore`
- Track `activeProfileId` in the profile loop
- Render `BrowserActionList` inside `AddressBarExtensions` when profile has extensions
- Moved `AddressBarExtensions` before `AddressBarProfileBadge` so extension icons appear to the left of the profile badge

- [ ] **Step 2: Verify it compiles**

```bash
cd /Users/andrevictor/www/pane/apps/desktop && npx tsc --noEmit
```

- [ ] **Step 3: Commit**

```bash
cd /Users/andrevictor/www/pane
git add apps/desktop/src/renderer/components/address-bar/address-bar-connected.tsx
git commit -m "$(cat <<'EOF'
feat: render extension icons in address bar
EOF
)"
```

---

## Task 12: Wire everything in the orchestrator

**Files:**
- Modify: `apps/desktop/src/main/index.ts`

- [ ] **Step 1: Update index.ts with ExtensionManager wiring**

Replace the entire `apps/desktop/src/main/index.ts` with:

```ts
import path from "node:path";
import {
  app,
  BaseWindow,
  ipcMain,
  Menu,
  WebContentsView,
} from "electron";

import { extensionStore } from "../stores/extension-store";
import { navigationStore } from "../stores/navigation-store";
import { profileStore } from "../stores/profile-store";
import { settingsStore } from "../stores/settings-store";
import { tabStore } from "../stores/tab-store";
import { detectBrowserPath } from "./browser/detect-browser";
import { TabManager } from "./browser/tab-manager";
import { ExtensionManager } from "./extensions/extension-manager";
import { StoreSync } from "./store-sync";

let mainWindow: BaseWindow | null = null;
let uiView: WebContentsView | null = null;

const tabManager = new TabManager();

const storeSync = new StoreSync({
  "profile-store": profileStore,
  "tab-store": tabStore,
  "navigation-store": navigationStore,
  "settings-store": settingsStore,
  "extension-store": extensionStore,
});

let extensionManager: ExtensionManager | null = null;

function createWindow() {
  mainWindow = new BaseWindow({
    width: 1280,
    height: 800,
    minWidth: 900,
    minHeight: 600,
    title: "Pane",
    titleBarStyle: "hiddenInset",
    backgroundColor: "#09090b",
  });

  uiView = new WebContentsView({
    webPreferences: {
      preload: path.join(__dirname, "../preload/index.mjs"),
      contextIsolation: true,
      sandbox: false,
    },
  });

  storeSync.connect(uiView.webContents);
  tabManager.attach(mainWindow);

  extensionManager = new ExtensionManager(tabManager, mainWindow);

  tabManager.onTabCreated = (wc, profileId) =>
    extensionManager!.registerTab(wc, profileId);
  tabManager.onTabActivated = (wc) => extensionManager!.activateTab(wc);
  tabManager.onTabRemoved = (wc) => extensionManager!.unregisterTab(wc);

  mainWindow.contentView.addChildView(uiView);

  const [width, height] = mainWindow.getContentSize();
  uiView.setBounds({ x: 0, y: 0, width, height });

  if (process.env.ELECTRON_RENDERER_URL) {
    uiView.webContents.loadURL(process.env.ELECTRON_RENDERER_URL);
  } else {
    uiView.webContents.loadFile(path.join(__dirname, "../renderer/index.html"));
  }

  mainWindow.on("resized", () => {
    if (!mainWindow) {
      return;
    }

    const [w, h] = mainWindow.getContentSize();
    uiView?.setBounds({ x: 0, y: 0, width: w, height: h });
    tabManager.resizeAll();
  });

  mainWindow.on("closed", () => {
    mainWindow = null;
    uiView = null;
    tabManager.clear();
  });
}

app.whenReady().then(() => {
  profileStore.persist.rehydrate();
  settingsStore.persist.rehydrate();

  if (!settingsStore.getState().settings.chromiumPath) {
    const detected = detectBrowserPath();

    if (detected) {
      settingsStore.getState().save({ chromiumPath: detected });
    }
  }

  storeSync.register();
  tabManager.registerIpc();

  ipcMain.handle("settings:detect-browser", () => {
    const detected = detectBrowserPath();

    if (detected) {
      settingsStore.getState().save({ chromiumPath: detected });
    }

    return detected;
  });

  const menu = Menu.buildFromTemplate([
    { role: "appMenu" },
    { role: "fileMenu" },
    { role: "editMenu" },
    {
      label: "View",
      submenu: [
        {
          label: "Toggle developer tools",
          accelerator: "CommandOrControl+Option+I",
          click: () => uiView?.webContents.openDevTools({ mode: "detach" }),
        },
        { role: "reload", click: () => uiView?.webContents.reload() },
        { type: "separator" },
        { role: "resetZoom" },
        { role: "zoomIn" },
        { role: "zoomOut" },
        { type: "separator" },
        { role: "togglefullscreen" },
      ],
    },
    { role: "windowMenu" },
  ]);
  Menu.setApplicationMenu(menu);

  createWindow();
  extensionManager?.registerIpc();
  tabManager.restore();

  profileStore.subscribe((state, prev) => {
    const removedIds = prev.profiles
      .filter((p) => !state.profiles.find((s) => s.id === p.id))
      .map((p) => p.id);

    for (const id of removedIds) {
      extensionManager?.destroyProfile(id);
    }
  });

  app.on("activate", () => {
    if (!mainWindow) {
      createWindow();
      tabManager.restore();
    }
  });
});

app.on("window-all-closed", () => {
  if (process.platform !== "darwin") {
    app.quit();
  }
});
```

Changes from the original:
- Import `extensionStore` and `ExtensionManager`
- Add `extensionStore` to `StoreSync`
- Create `ExtensionManager` in `createWindow()` after `tabManager.attach()`
- Wire three callback hooks
- Subscribe to `profileStore` for profile deletion cleanup

- [ ] **Step 2: Verify the full app builds**

```bash
cd /Users/andrevictor/www/pane/apps/desktop && npm run build
```

Expected: build succeeds with no errors.

- [ ] **Step 3: Commit**

```bash
cd /Users/andrevictor/www/pane
git add apps/desktop/src/main/index.ts
git commit -m "$(cat <<'EOF'
feat: wire ExtensionManager into main process orchestrator
EOF
)"
```

---

## Task 13: Smoke test — launch and verify NordPass loads

This is a manual verification task. No code changes — just run the app and verify the integration works.

- [ ] **Step 1: Start the dev server**

```bash
cd /Users/andrevictor/www/pane/apps/desktop && npm run dev
```

- [ ] **Step 2: Create a profile and open a tab**

In the app UI, create a new profile. Open a tab for it. This should trigger lazy ECE instantiation and auto-load the NordPass extension from `extensions/`.

- [ ] **Step 3: Verify NordPass service worker boots**

Open DevTools (Cmd+Option+I). Check the console for:
- No `Cannot read properties of undefined (reading 'onClicked')` errors
- No `chrome.alarms is not defined` errors
- No `chrome.idle is not defined` errors

If there are errors, debug by checking:
- Is the shim preload being loaded? Check `out/preload/shim-preload.mjs` exists
- Is the preload running before ECE's? Check registration order in `getOrCreateInstance`
- Are the IPC handlers registered? Check main process console for errors

- [ ] **Step 4: Verify extension icon appears in address bar**

The NordPass icon should appear in the address bar between the URL input and the profile badge.

- [ ] **Step 5: Verify popup opens**

Click the NordPass extension icon. A popup window should appear below the icon showing the NordPass login UI (email input, Continue button).

- [ ] **Step 6: Test NordPass login flow**

Enter your NordPass credentials. The login should work via HTTPS to `api.nordpass.com`. After login, the vault should appear in the popup.

- [ ] **Step 7: Test autofill**

Navigate to a login page (e.g., github.com/login). NordPass should detect the form and offer autofill.

- [ ] **Step 8: Document any issues**

If any step fails, note the error and which component needs fixing. Common issues:
- `injectBrowserAction` import path wrong → check `node_modules/electron-chrome-extensions/dist/` for correct path
- Shim preload not found → check `SHIM_PRELOAD_PATH` resolves to `out/preload/shim-preload.mjs`
- Extension not loading → check `EXTENSIONS_DIR` resolves to `apps/desktop/extensions/`
- Popup not positioned correctly → check if `<browser-action-list>` element renders in DOM

---

## Task 14: Fix build issues from smoke test

This task is a placeholder for fixing any issues discovered during the smoke test. Common adjustments:

- [ ] **Step 1: Fix import paths if needed**

The `injectBrowserAction` import in the preload may need adjustment depending on how `electron-chrome-extensions` exports it. Check:

```bash
node -e "const m = require('electron-chrome-extensions'); console.log(Object.keys(m))"
```

If `injectBrowserAction` is exported from the main package entry point, change the import in `preload/index.ts` to:

```ts
import { injectBrowserAction } from "electron-chrome-extensions";
```

- [ ] **Step 2: Fix EXTENSIONS_DIR path if needed**

`app.getAppPath()` returns different paths in dev vs production. In dev mode with electron-vite, it returns the project root. In production, it returns the `app.asar` path. The extensions directory should be relative to the app root.

If the path is wrong, update `extension-manager.ts`:

```ts
import { app } from "electron";

const EXTENSIONS_DIR = app.isPackaged
  ? path.join(process.resourcesPath, "extensions")
  : path.join(app.getAppPath(), "extensions");
```

And add the extensions directory to the electron-builder config in `package.json`:

```json
"build": {
  "extraResources": [
    { "from": "extensions", "to": "extensions" }
  ]
}
```

- [ ] **Step 3: Rebuild and verify**

```bash
cd /Users/andrevictor/www/pane/apps/desktop && npm run build && npm run dev
```

- [ ] **Step 4: Commit all fixes**

```bash
cd /Users/andrevictor/www/pane
git add -A
git commit -m "$(cat <<'EOF'
fix: resolve build and path issues from smoke test
EOF
)"
```
