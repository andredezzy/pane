# ECE Consolidation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move all extension shim code into the ECE fork so it becomes a batteries-included library. Consumer API stays unchanged — just `createTab`/`selectTab`/`removeTab` callbacks.

**Architecture:** Preloads (`sw.js`, `frame.js`) and Chrome API shim handlers (`alarms`, `idle`, `windows`, `tabs`) move from `apps/desktop/` into `packages/electron-chrome-extensions/`. Shim handlers use `ExtensionContext` (ECE's internal context) instead of external `ShimDeps`. ECE's constructor registers preloads and IPC handlers internally. IPC channels rename from `pane-shim` to `crx-shim`.

**Tech Stack:** Electron 41, TypeScript, esbuild

**Spec:** `docs/superpowers/specs/2026-05-01-ece-consolidation-design.md`

---

### Task 1: Create shims directory and move handler files into ECE

**Files:**
- Create: `packages/electron-chrome-extensions/src/browser/shims/alarms.ts`
- Create: `packages/electron-chrome-extensions/src/browser/shims/idle.ts`
- Create: `packages/electron-chrome-extensions/src/browser/shims/windows.ts`
- Create: `packages/electron-chrome-extensions/src/browser/shims/tabs.ts`
- Create: `packages/electron-chrome-extensions/src/browser/shims/handler.ts`

- [ ] **Step 1: Copy alarms.ts and idle.ts as-is (no changes needed)**

Copy `apps/desktop/src/main/extensions/shims/alarms.ts` → `packages/electron-chrome-extensions/src/browser/shims/alarms.ts`

Copy `apps/desktop/src/main/extensions/shims/idle.ts` → `packages/electron-chrome-extensions/src/browser/shims/idle.ts`

These files are self-contained — they only import from `electron` and have no external deps.

- [ ] **Step 2: Create windows.ts using ExtensionContext instead of ShimDeps**

Create `packages/electron-chrome-extensions/src/browser/shims/windows.ts`:

```typescript
import { type Session } from "electron";
import type { ExtensionContext } from "../context";

function resolveExtensionUrl(ses: Session, url: string): string {
  if (url.startsWith("chrome-extension://") || url.startsWith("http")) return url;
  const ext = ses.extensions.getAllExtensions()[0];
  if (!ext) return url;
  return `chrome-extension://${ext.id}/${url.replace(/^\//, "")}`;
}

function makeWindowObject(ctx: ExtensionContext): Record<string, unknown> {
  const win = ctx.store.getLastFocusedWindow();
  return {
    id: win?.id ?? 1,
    focused: win ? win.isFocused() : true,
    top: 0,
    left: 0,
    width: 1280,
    height: 800,
    type: "normal",
    state: "normal",
  };
}

export function handleWindows(
  ctx: ExtensionContext,
  method: string,
  ...args: unknown[]
): unknown {
  switch (method) {
    case "create": {
      const [opts] = args as [{ url?: string; type?: string }];
      const url = opts?.url
        ? resolveExtensionUrl(ctx.session, opts.url)
        : "about:blank";
      ctx.store
        .createTab({ url })
        .catch(() => {});
      return makeWindowObject(ctx);
    }

    case "get":
    case "getCurrent":
    case "getLastFocused":
      return makeWindowObject(ctx);

    case "getAll":
      return [makeWindowObject(ctx)];

    case "update":
      return makeWindowObject(ctx);

    case "remove":
      return undefined;

    default:
      return undefined;
  }
}
```

- [ ] **Step 3: Create tabs.ts using ExtensionContext**

Create `packages/electron-chrome-extensions/src/browser/shims/tabs.ts`:

```typescript
import { type Session } from "electron";
import type { ExtensionContext } from "../context";

function resolveExtensionUrl(ses: Session, url: string): string {
  if (url.startsWith("chrome-extension://") || url.startsWith("http")) return url;
  const ext = ses.extensions.getAllExtensions()[0];
  if (!ext) return url;
  return `chrome-extension://${ext.id}/${url.replace(/^\//, "")}`;
}

function makeTabObject(
  tabId: number,
  url: string,
  active: boolean,
): Record<string, unknown> {
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
  ctx: ExtensionContext,
  method: string,
  ...args: unknown[]
): unknown {
  switch (method) {
    case "create": {
      const [opts] = args as [{ url?: string; active?: boolean }];
      const url = opts?.url
        ? resolveExtensionUrl(ctx.session, opts.url)
        : "about:blank";
      ctx.store
        .createTab({ url })
        .catch(() => {});
      return makeTabObject(0, url, true);
    }

    case "get": {
      const [tabId] = args as [number];
      return makeTabObject(tabId, "", false);
    }

    case "query": {
      const activeTab = ctx.store.getActiveTabOfCurrentWindow();
      if (!activeTab) return [];
      return [
        makeTabObject(activeTab.id, activeTab.getURL(), true),
      ];
    }

    case "update":
      return makeTabObject(0, "", false);

    case "remove":
      return undefined;

    default:
      return undefined;
  }
}
```

- [ ] **Step 4: Create handler.ts with crx-shim IPC dispatch**

Create `packages/electron-chrome-extensions/src/browser/shims/handler.ts`:

```typescript
import { ipcMain, type Session } from "electron";
import type { ExtensionContext } from "../context";
import { handleAlarms } from "./alarms";
import { handleIdle } from "./idle";
import { handleWindows } from "./windows";
import { handleTabs } from "./tabs";

let globalRegistered = false;

export function registerShimHandler(ctx: ExtensionContext) {
  if (globalRegistered) return;
  globalRegistered = true;

  ipcMain.handle(
    "crx-shim",
    (event, namespace: string, method: string, ...args: unknown[]) => {
      return dispatch(ctx, namespace, method, ...args);
    },
  );
}

const registeredSessions = new WeakSet<Session>();

export function registerShimHandlerForSession(ctx: ExtensionContext) {
  const ses = ctx.session;
  if (registeredSessions.has(ses)) return;
  registeredSessions.add(ses);

  const workers = new WeakSet();

  ses.serviceWorkers.on("running-status-changed", ({
    runningStatus,
    versionId,
  }: Electron.Event<Electron.ServiceWorkersRunningStatusChangedEventParams>) => {
    if (runningStatus !== "starting") return;

    const sw = (ses as any).serviceWorkers.getWorkerFromVersionID(versionId);
    if (!sw || workers.has(sw)) return;
    if (!sw.scope?.startsWith("chrome-extension://")) return;

    workers.add(sw);
    sw.ipc.handle(
      "crx-shim",
      (_event: unknown, namespace: string, method: string, ...args: unknown[]) => {
        return dispatch(ctx, namespace, method, ...args);
      },
    );
  });
}

function dispatch(
  ctx: ExtensionContext,
  namespace: string,
  method: string,
  ...args: unknown[]
) {
  switch (namespace) {
    case "alarms":
      return handleAlarms(ctx.session, method, ...args);
    case "idle":
      return handleIdle(ctx.session, method, ...args);
    case "windows":
      return handleWindows(ctx, method, ...args);
    case "tabs":
      return handleTabs(ctx, method, ...args);
    case "action": {
      if (method === "setPopup") {
        const [details] = args as [{ popup?: string }];
        const ext = ctx.session.extensions.getAllExtensions()[0];
        if (ext && details?.popup) {
          ctx.emit("set-popup", ext.id, details.popup);
        }
      }
      return undefined;
    }
    default:
      return undefined;
  }
}
```

- [ ] **Step 5: Verify TypeScript compiles**

Run: `cd packages/electron-chrome-extensions && npx tsc --noEmit 2>&1 | grep shims | head -10`
Expected: No errors in shims files

- [ ] **Step 6: Commit**

```bash
git add packages/electron-chrome-extensions/src/browser/shims/
git commit -m "feat(ece): add Chrome API shim handlers (alarms, idle, windows, tabs)"
```

---

### Task 2: Move preload files into ECE

**Files:**
- Create: `packages/electron-chrome-extensions/src/preloads/sw.js`
- Create: `packages/electron-chrome-extensions/src/preloads/frame.js`

- [ ] **Step 1: Copy sw-preload.js → src/preloads/sw.js**

Copy `apps/desktop/resources/sw-preload.js` → `packages/electron-chrome-extensions/src/preloads/sw.js`

Then rename all IPC channels in the file:
- `pane-shim` → `crx-shim`
- `pane-shim-event` → `crx-shim-event`
- `__paneIpc` → `__crxIpc`
- `__paneEvents` → `__crxEvents`

Search-and-replace these four strings in `sw.js`.

- [ ] **Step 2: Copy shim-preload.js → src/preloads/frame.js**

Copy `apps/desktop/resources/shim-preload.js` → `packages/electron-chrome-extensions/src/preloads/frame.js`

Then rename all IPC channels in the file:
- `pane-shim` → `crx-shim`
- `__paneIpc` → `__crxIpc`

Search-and-replace these two strings in `frame.js`.

- [ ] **Step 3: Verify syntax**

Run: `node --check packages/electron-chrome-extensions/src/preloads/sw.js && node --check packages/electron-chrome-extensions/src/preloads/frame.js && echo "OK"`
Expected: `OK`

- [ ] **Step 4: Commit**

```bash
git add packages/electron-chrome-extensions/src/preloads/
git commit -m "feat(ece): add sw.js and frame.js preloads"
```

---

### Task 3: Update esbuild.config.cjs to copy preload files

**Files:**
- Modify: `packages/electron-chrome-extensions/esbuild.config.cjs`

- [ ] **Step 1: Add fs import and copy step**

Add to the top of the file:
```javascript
const fs = require('fs')
const path = require('path')
```

Add after `Promise.all(...)`:
```javascript
.then(() => {
  // Copy preload files (not bundled — they use require("electron"))
  const preloadsDir = path.join(__dirname, 'dist', 'preloads')
  fs.mkdirSync(preloadsDir, { recursive: true })
  fs.copyFileSync(
    path.join(__dirname, 'src', 'preloads', 'sw.js'),
    path.join(preloadsDir, 'sw.js')
  )
  fs.copyFileSync(
    path.join(__dirname, 'src', 'preloads', 'frame.js'),
    path.join(preloadsDir, 'frame.js')
  )
  console.log('electron-chrome-extensions built successfully')
})
```

Remove the existing `.then(() => console.log(...))`.

- [ ] **Step 2: Build and verify**

Run: `cd packages/electron-chrome-extensions && node esbuild.config.cjs && ls dist/preloads/`
Expected: `frame.js  sw.js`

- [ ] **Step 3: Commit**

```bash
git add packages/electron-chrome-extensions/esbuild.config.cjs
git commit -m "build(ece): copy preload files to dist/preloads/"
```

---

### Task 4: Wire preloads and shim handlers into ECE's constructor

**Files:**
- Modify: `packages/electron-chrome-extensions/src/browser/index.ts`

- [ ] **Step 1: Update imports**

Add to the imports at the top of `index.ts`:

```typescript
import { registerShimHandler, registerShimHandlerForSession } from './shims/handler'
```

- [ ] **Step 2: Rewrite prependPreload to register our preloads**

Replace the entire `prependPreload` method with:

```typescript
private prependPreload() {
  const { session } = this.ctx
  const preloadsDir = path.join(__dirname, '..', 'preloads')

  if ('registerPreloadScript' in session) {
    session.registerPreloadScript({
      id: 'crx-frame',
      type: 'frame',
      filePath: path.join(preloadsDir, 'frame.js'),
    })
    session.registerPreloadScript({
      id: 'crx-sw',
      type: 'service-worker',
      filePath: path.join(preloadsDir, 'sw.js'),
    })
  }
}
```

- [ ] **Step 3: Register shim handlers in constructor**

In the constructor, after `this.prependPreload()`, add:

```typescript
registerShimHandler(this.ctx)
registerShimHandlerForSession(this.ctx)
```

- [ ] **Step 4: Wire action.setPopup from SW shim**

In the constructor, after creating `this.api`, add:

```typescript
this.on('set-popup', (extensionId: string, popup: string) => {
  this.api.browserAction.setPopupUrl(extensionId, popup)
})
```

- [ ] **Step 5: Handle browser-action-clicked internally**

In the constructor, add:

```typescript
this.on('browser-action-clicked', (extensionId: string) => {
  const ext = session.extensions.getExtension(extensionId)
  if (!ext) return

  const page =
    ext.manifest.options_ui?.page ||
    ext.manifest.options_page ||
    ext.manifest.action?.default_popup ||
    this.findExtensionPage(ext.path)
  if (!page) return

  const url = `chrome-extension://${extensionId}/${page}`
  this.ctx.store.createTab({ url }).catch(() => {})
})
```

- [ ] **Step 6: Add findExtensionPage utility and default popup on extension-loaded**

Add as a private method:

```typescript
private findExtensionPage(extPath: string): string | null {
  for (const name of ['app.html', 'popup.html', 'main.html', 'index.html']) {
    if (existsSync(path.join(extPath, name))) return name
  }
  return null
}
```

Update `listenForExtensions` to set default popup:

```typescript
private listenForExtensions() {
  const sessionExtensions = this.ctx.session.extensions || this.ctx.session
  sessionExtensions.addListener('extension-loaded', (_event, extension) => {
    readLoadedExtensionManifest(this.ctx, extension)

    if (!extension.manifest.action?.default_popup) {
      const popup = ['index.html', 'popup.html'].find(
        (name) => existsSync(path.join(extension.path, name)),
      )
      if (popup) {
        this.api.browserAction.setPopupUrl(
          extension.id,
          `chrome-extension://${extension.id}/${popup}`,
        )
      }
    }
  })
}
```

- [ ] **Step 7: Add destroy method for cleanup**

Add a public `destroy` method to `ElectronChromeExtensions`:

```typescript
destroy() {
  const ses = this.ctx.session
  destroyAlarms(ses)
  destroyIdle(ses)
  sessionMap.delete(ses)
}
```

Add import at the top:
```typescript
import { destroyAlarms } from './shims/alarms'
import { destroyIdle } from './shims/idle'
```

- [ ] **Step 9: Update prependPreload call (remove modulePath parameter)**

In the constructor, change:
```typescript
this.prependPreload(opts.modulePath)
```
to:
```typescript
this.prependPreload()
```

- [ ] **Step 10: Verify TypeScript compiles**

Run: `cd packages/electron-chrome-extensions && npx tsc --noEmit 2>&1 | head -10`
Expected: No errors

- [ ] **Step 11: Build ECE**

Run: `cd packages/electron-chrome-extensions && node esbuild.config.cjs`
Expected: `electron-chrome-extensions built successfully`

- [ ] **Step 12: Commit**

```bash
git add packages/electron-chrome-extensions/src/browser/index.ts
git commit -m "feat(ece): register preloads and shim handlers internally"
```

---

### Task 5: Rename IPC channels in shim handler

**Files:**
- Modify: `packages/electron-chrome-extensions/src/browser/shims/handler.ts`
- Modify: `packages/electron-chrome-extensions/src/browser/shims/alarms.ts`
- Modify: `packages/electron-chrome-extensions/src/browser/shims/idle.ts`

- [ ] **Step 1: Rename in handler.ts**

The handler already uses `crx-shim` (from Task 1). Verify:

```bash
grep "pane-shim" packages/electron-chrome-extensions/src/browser/shims/*.ts
```

Expected: No results (all should use `crx-shim`)

- [ ] **Step 2: Rename event channel in alarms.ts and idle.ts**

In `alarms.ts`, replace `pane-shim-event` → `crx-shim-event`:
```bash
sed -i '' 's/pane-shim-event/crx-shim-event/g' packages/electron-chrome-extensions/src/browser/shims/alarms.ts
```

In `idle.ts`, replace `pane-shim-event` → `crx-shim-event`:
```bash
sed -i '' 's/pane-shim-event/crx-shim-event/g' packages/electron-chrome-extensions/src/browser/shims/idle.ts
```

- [ ] **Step 3: Verify**

```bash
grep -r "pane-shim" packages/electron-chrome-extensions/src/
```

Expected: No results

- [ ] **Step 4: Commit**

```bash
git add packages/electron-chrome-extensions/src/browser/shims/
git commit -m "refactor(ece): rename IPC channels from pane-shim to crx-shim"
```

---

### Task 6: Strip ExtensionManager to ~50 lines

**Files:**
- Modify: `apps/desktop/src/main/extensions/extension-manager.ts`

- [ ] **Step 1: Rewrite ExtensionManager**

Replace the entire file with:

```typescript
import {
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

export class ExtensionManager {
  private readonly instances = new Map<string, ElectronChromeExtensions>();
  private readonly webContentsToProfile = new WeakMap<WebContents, string>();

  constructor(
    private readonly tabManager: TabManager,
    private readonly mainWindow: BaseWindow,
  ) {}

  registerTab(webContents: WebContents, profileId: string) {
    this.webContentsToProfile.set(webContents, profileId);
    this.getOrCreateInstance(profileId).addTab(webContents, this.mainWindow);
  }

  activateTab(webContents: WebContents) {
    const profileId = this.webContentsToProfile.get(webContents);
    if (profileId) this.instances.get(profileId)?.selectTab(webContents);
  }

  unregisterTab(webContents: WebContents) {
    const profileId = this.webContentsToProfile.get(webContents);
    if (profileId) this.instances.get(profileId)?.removeTab(webContents);
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
    this.instances.get(profileId)?.destroy();
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
    ElectronChromeExtensions.handleCRXProtocol(ses);

    const ece = new ElectronChromeExtensions({
      license: "GPL-3.0",
      session: ses,
      createTab: async (details) => {
        const profile = profileStore
          .getState()
          .profiles.find((p) => p.id === profileId);
        if (!profile) throw new Error(`Profile ${profileId} not found`);
        const view = this.tabManager.createViewForExtension(
          crypto.randomUUID(),
          profile,
          details.url || "about:blank",
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

- [ ] **Step 2: Verify TypeScript compiles**

Run: `cd apps/desktop && npx tsc --noEmit 2>&1 | grep extension-manager`
Expected: No errors

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/src/main/extensions/extension-manager.ts
git commit -m "refactor: strip ExtensionManager to thin orchestrator (~80 lines)"
```

---

### Task 7: Delete moved files from desktop app

**Files:**
- Delete: `apps/desktop/resources/sw-preload.js`
- Delete: `apps/desktop/resources/shim-preload.js`
- Delete: `apps/desktop/src/main/extensions/shims/` (entire directory)

- [ ] **Step 1: Delete files**

```bash
rm apps/desktop/resources/sw-preload.js
rm apps/desktop/resources/shim-preload.js
rm -rf apps/desktop/src/main/extensions/shims/
```

- [ ] **Step 2: Verify no stale references**

```bash
grep -rn "sw-preload\|shim-preload\|SHIM_PRELOAD\|SW_PRELOAD\|shims/handler\|shims/alarms\|shims/idle\|shims/windows\|shims/tabs\|registerShimHandler\|destroyAlarms\|destroyIdle\|pane-shim" apps/desktop/src/ apps/desktop/resources/ 2>/dev/null
```

Expected: No results

- [ ] **Step 3: Verify the `index.ts` has no references to removed code**

```bash
grep -n "shims\|PRELOAD_PATH\|registerShim\|destroyAlarms\|destroyIdle" apps/desktop/src/main/index.ts
```

Expected: No results (the auto-load test code should be clean)

- [ ] **Step 4: Commit**

```bash
git add -A apps/desktop/resources/ apps/desktop/src/main/extensions/shims/
git commit -m "chore: delete shim files moved into ECE fork"
```

---

### Task 8: Build and test end-to-end

**Files:** None — integration verification

- [ ] **Step 1: Rebuild ECE**

```bash
cd packages/electron-chrome-extensions && node esbuild.config.cjs
```

Expected: `electron-chrome-extensions built successfully`

- [ ] **Step 2: Verify preloads in dist**

```bash
ls -la packages/electron-chrome-extensions/dist/preloads/
```

Expected: `sw.js` and `frame.js` present

- [ ] **Step 3: Build desktop app**

```bash
cd apps/desktop && npx electron-vite build
```

Expected: Build succeeds

- [ ] **Step 4: Launch and test**

```bash
rm -rf "$HOME/Library/Application Support/@pane"
cd apps/desktop && npx electron-vite dev
```

Create a profile. Verify:
- Extension loads (N icon appears in address bar)
- Click N → popup or app.html tab opens
- No `pane-shim` references in console (should be `crx-shim` now)
- No errors about missing preload files

- [ ] **Step 5: Commit checkpoint**

```bash
git add -A
git commit -m "feat: ECE consolidation complete — batteries-included extension support"
```

---

### Task 9: Update documentation

**Files:**
- Modify: `apps/desktop/docs/extension-support.md`

- [ ] **Step 1: Update docs to reflect ECE consolidation**

Update `apps/desktop/docs/extension-support.md`:
- Remove all references to `apps/desktop/resources/sw-preload.js` and `shim-preload.js`
- Remove `apps/desktop/src/main/extensions/shims/` from the architecture diagram
- Add `packages/electron-chrome-extensions/src/preloads/` and `src/browser/shims/` to the diagram
- Update IPC channel names from `pane-shim` to `crx-shim`
- Note that ECE handles everything internally — consumer just provides callbacks
- Simplify the "ExtensionManager" section to show the ~80-line version

- [ ] **Step 2: Commit**

```bash
git add apps/desktop/docs/extension-support.md
git commit -m "docs: update extension-support.md for ECE consolidation"
```
