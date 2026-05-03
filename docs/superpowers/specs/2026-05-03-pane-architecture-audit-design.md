# Pane Architecture Audit — Implementation Spec

Five sub-projects addressing 26 findings from security, architecture, performance, and DX audits. Each sub-project is independent and ordered by priority: SP1 (security) → SP2 (stability) → SP3 (fingerprinting) → SP4 (performance) → SP5 (DX).

---

## SP1: Security Hardening

### 1.1 — Enable sandbox on UI view

**File:** `src/main/window.ts:38`

Change `sandbox: false` to `sandbox: true` on the UI `WebContentsView`. Audit `injectBrowserAction` from `@pane/electron-chrome-extensions/browser-action` — if it uses Node APIs, refactor to receive data via IPC. The `contextBridge` calls in `preload/index.ts` already work in sandbox mode.

Sandboxed preload scripts can only use: `events`, `timers`, `url`, polyfilled `Buffer`/`process`, plus `ipcRenderer` and `contextBridge` from Electron.

### 1.2 — IPC sender validation

**File:** `src/main/ipc.ts`

Add a guard that checks `event.sender.id === trustedWebContentsId` at the top of each `ipcMain.handle` callback. The trusted ID comes from `uiView.webContents.id`, passed to `IpcRouter` at construction time. Reject calls from any other sender silently.

```ts
export class IpcRouter {
  constructor(
    private readonly pane: Pane,
    private readonly trustedSenderId: number,
  ) {}

  private isTrusted(event: Electron.IpcMainInvokeEvent): boolean {
    return event.sender.id === this.trustedSenderId;
  }

  register(): void {
    ipcMain.handle("tabs:open", (event, profileId, url) => {
      if (!this.isTrusted(event)) return;
      // ...
    });
  }
}
```

Update call site in `index.ts`: `new IpcRouter(pane, win.uiView.webContents.id).register()`.

### 1.3 — Validate `sync:push` input

**File:** `src/stores/middlewares/store-sync.ts`

Add sender validation (same trusted ID pattern as 1.2, passed to `StoreSync` constructor) plus:
- Verify `data.store` is a known store name from the registered `stores` map
- Wrap `JSON.parse(data.state)` in try/catch, reject malformed payloads

### 1.4 — Configure Electron Fuses

**Files:** `package.json` (dev dep), packaging script

Add `@electron/fuses` as a dev dependency. In the electron-builder / packaging config, apply fuses:

- Disable: `RunAsNode`, `EnableNodeOptionsEnvironmentVariable`, `EnableNodeCliInspectArguments`
- Enable: `CookieEncryption`, `OnlyLoadAppFromAsar`

Build-time only — no runtime code changes.

### 1.5 — Surface proxy errors

**File:** `src/main/profile/profile-tabs.ts:201`

Replace `.catch(() => {})` on `session.setProxy()` with:
- Log the error with `console.error`
- Send an IPC notification to the renderer so the UI can display a toast/warning that the proxy failed to apply for this profile

---

## SP2: Architecture Fixes

### 2.1 — Fix duplicate IPC handler registration on macOS re-activate

**Files:** `src/main/ipc.ts`, `src/main/index.ts`

Add a `cleanup()` method to `IpcRouter` that calls `ipcMain.removeHandler(channel)` for every registered channel. Store channel names in an array during `register()`.

Call `ipcRouter.cleanup()` in `win.mainWindow.on("closed")` before setting `pane = null`.

### 2.2 — Fix `createProfile` array tail assumption

**Files:** `src/main/pane.ts:18-26`, `src/stores/profile-store.ts`

Change `profileStore.getState().create(input)` to return the new profile's ID:

```ts
create: (input) => {
  const id = crypto.randomUUID();
  const now = new Date().toISOString();
  set((s) => ({
    profiles: [...s.profiles, { ...input, id, tabs: [], isExpanded: false, createdAt: now, updatedAt: now }],
  }));
  return id;
},
```

In `pane.ts`, look up by ID: `const profile = profiles.find(p => p.id === id)`.

### 2.3 — Fix extension update check using arbitrary profile session

**File:** `src/main/extension-installer.ts:155-166`

Create a dedicated neutral session for extension update network requests:

```ts
const updateSession = session.fromPartition("persist:pane-internal");
```

Use this session instead of `profiles[0].session` for `updateExtensions()` calls.

### 2.4 — Debounce and optimize state sync broadcasts

**File:** `src/stores/middlewares/store-sync.ts`

Add per-store debouncing (16ms window) to `broadcast()`:

```ts
private timers = new Map<string, ReturnType<typeof setTimeout>>();

private scheduleBroadcast(storeName: string, state: string) {
  const existing = this.timers.get(storeName);
  if (existing) clearTimeout(existing);
  this.timers.set(storeName, setTimeout(() => {
    this.timers.delete(storeName);
    this.broadcast(storeName, state);
  }, 16));
}
```

Replace the direct `broadcast` call in `store.subscribe()` with `scheduleBroadcast`.

---

## SP3: Fingerprint Engine

### 3.1 — Fingerprint preload script

**New file:** `src/main/profile/fingerprint-preload.ts`

A preload script registered via `session.registerPreloadScript({ type: 'frame' })` on each profile's session. Overrides:

| Property | Technique |
|---|---|
| `navigator.platform` | `Object.defineProperty(navigator, 'platform', { get: () => value })` |
| `navigator.hardwareConcurrency` | `Object.defineProperty` |
| `navigator.deviceMemory` | `Object.defineProperty` |
| `navigator.language` / `languages` | `Object.defineProperty` |
| `navigator.maxTouchPoints` | `Object.defineProperty` |
| `screen.width` / `height` / `colorDepth` | `Object.defineProperty` on `screen` |
| WebGL vendor/renderer | Wrap `WebGLRenderingContext.prototype.getParameter` — intercept `UNMASKED_VENDOR_WEBGL` (37446) and `UNMASKED_RENDERER_WEBGL` (37445) |
| Canvas fingerprint | Wrap `CanvasRenderingContext2D.prototype.getImageData` and `HTMLCanvasElement.prototype.toDataURL` — add deterministic per-profile noise via seeded PRNG |
| AudioContext fingerprint | Wrap `OfflineAudioContext.prototype.startRendering` — add deterministic noise to output buffer samples |

Timezone is set from the main process via `session.setTimezoneOffset(minutes)` — not in the preload. This affects `Intl.DateTimeFormat` and `Date` across the entire session. Language is also set from main via `session.setSpellCheckerLanguages()`.

### 3.2 — Config delivery mechanism

When a profile activates (in `Profile` constructor), write a temporary JS file to `app.getPath('temp')` that embeds the serialized fingerprint config as a const:

```js
// Generated file
const __PANE_FP__ = {"platform":"Win32","hardwareConcurrency":8,...};
// ... override code follows
```

Register the file path via `session.registerPreloadScript({ type: 'frame', filePath: tmpPath })`. This avoids IPC timing issues since preload runs before IPC channels are available.

Clean up temp files on profile deactivation or app quit.

### 3.3 — Cross-API consistency validation

**New file:** `src/main/profile/fingerprint-validator.ts`

A `validateFingerprint(config: Fingerprint): string[]` function that checks consistency rules:

- `platform === "windows"` → WebGL renderer must not contain "Apple" / "M1" / "M2" / "M3"
- `platform === "macos"` → WebGL renderer must be Apple GPU or known Mac discrete GPU
- `hardwareConcurrency` must be power of 2, between 1-128
- `deviceMemory` must be one of: 0.25, 0.5, 1, 2, 4, 8
- `language` region should match `timezone` region (e.g., `pt-BR` + `America/Sao_Paulo`)
- `maxTouchPoints` should be 0 for desktop platforms, > 0 for mobile

Returns a list of warning strings. Called during profile creation/edit. Surface warnings in the UI (non-blocking — user can override).

### 3.4 — Fingerprint interface extension

**File:** `src/stores/profile-store.ts`

Extend the existing `Fingerprint` interface:

```ts
export interface Fingerprint {
  userAgent: string;
  platform: "windows" | "macos" | "linux";
  screen: { width: number; height: number; colorDepth: number };
  language: string;
  languages: string[];
  timezone: string;
  webgl: { vendor: string; renderer: string } | null;
  hardwareConcurrency: number;
  deviceMemory: number;
  maxTouchPoints: number;
  canvas: { noise: boolean };
  audio: { noise: boolean };
}
```

Added fields: `screen.colorDepth`, `canvas`, `audio`. Update `default-fingerprints.ts` to include these new fields in each template.

---

## SP4: Performance

### 4.1 — Set proxy once per session

**Files:** `src/main/profile/profile.ts`, `src/main/profile/profile-tabs.ts`

Move `session.setProxy(proxyConfig)` from `ProfileTabs.createView()` to `Profile` constructor. The session is shared across all tabs in a profile — set proxy once when the session is created.

### 4.2 — Add tabId → profileId index

**File:** `src/main/pane.ts`

Add a `Map<string, string>` (`tabId → profileId`) to `Pane`:

```ts
private readonly tabIndex = new Map<string, string>();
```

Update on `openTab` (add entry), `closeTab` (delete entry). Add `getProfileForTab(tabId: string): Profile | undefined` that does O(1) lookup.

Replace all linear scans in `ipc.ts` (`tabs:switch`, `tabs:close`) and `profile-tabs.ts` (`close()`) with index lookups.

### 4.3 — Make `getInstalled()` async

**File:** `src/main/extension-installer.ts:58-151`

Replace all sync fs calls with `fs/promises` equivalents:
- `readdirSync` → `readdir`
- `statSync` → `stat`
- `existsSync` + `readFileSync` → `readFile` with catch
- Return `Promise<InstalledExtension[]>` instead of `InstalledExtension[]`

The IPC handler already uses `ipcMain.handle` (returns Promise), so no call-site changes needed.

### 4.4 — Granular sidebar selectors

**File:** `src/renderer/components/sidebar/sidebar-connected.tsx`

Replace `useStore(profileStore, s => s.profiles)` with:

1. Top-level component subscribes to profile IDs only (using `useShallow`):
   ```ts
   const profileIds = useStore(profileStore, useShallow(s => s.profiles.map(p => p.id)));
   ```

2. Each `ProfileItem` subscribes to its own profile by ID:
   ```ts
   function ConnectedProfileItem({ id }: { id: string }) {
     const profile = useStore(profileStore, s => s.profiles.find(p => p.id === id));
     // ...
   }
   ```

A favicon change in profile A only re-renders profile A's component.

### 4.5 — Lazy tab restoration

**Files:** `src/main/pane.ts`, `src/stores/profile-store.ts`, `src/main/profile/profile-tabs.ts`

Add `isLoaded: boolean` to the `Tab` interface (default `false`, not persisted).

On `restore()`:
1. Rehydrate tab metadata (title, favicon, URL) into the store immediately — the sidebar shows these
2. Create `WebContentsView` only for the active tab
3. When user clicks an unloaded tab, create the `WebContentsView` on demand (in `tabs:switch` handler)

This means an app with 20 saved tabs creates 1 renderer process at startup, not 20.

### 4.6 — Destroy WebContentsView on window close

**File:** `src/main/index.ts:43-44`

In `win.mainWindow.on("closed")`:

```ts
if (pane) {
  for (const profile of pane.profiles.values()) {
    profile.tabs.destroyAll(); // new method: calls view.webContents.close() on each
  }
}
```

Add `destroyAll()` to `ProfileTabs` that iterates all views and calls `webContents.close()`.

### 4.7 — Flush fs-storage debounce on quit

**File:** `src/stores/middlewares/fs-storage.ts`, `src/main/index.ts`

Export a `flushPendingWrites()` function from `fs-storage.ts`:

```ts
export function flushPendingWrites(): void {
  for (const [name, timer] of debounceTimers) {
    clearTimeout(timer);
    debounceTimers.delete(name);
    const filePath = resolvePath(name);
    const tmpPath = `${filePath}.tmp`;
    require("node:fs").writeFileSync(tmpPath, /* last value */, "utf-8");
    require("node:fs").renameSync(tmpPath, filePath);
  }
}
```

This requires storing the pending value alongside the timer. Change `debounceTimers` to `Map<string, { timer, value }>`.

In `index.ts`, add: `app.on("before-quit", () => flushPendingWrites())`.

---

## SP5: DX & Modernization

### 5.1 — Extract layout constants

**New file:** `src/constants/layout.ts`

```ts
export const SIDEBAR_WIDTH = 220;
export const TOOLBAR_HEIGHT = 51;
export const PANEL_MARGIN_RIGHT = 8;
export const PANEL_MARGIN_BOTTOM = 8;
```

Import in `profile-tabs.ts`. For the renderer, expose as CSS custom properties via a shared constant or inline style on the root element.

### 5.2 — Sort extension version directories

**Files:** `src/main/profile/extension-loader.ts:52-58`, `src/main/index.ts:83`, `src/main/extension-installer.ts:129`

Replace `versions[0]` with:

```ts
const latest = versions.sort((a, b) => {
  const pa = a.split(".").map(Number);
  const pb = b.split(".").map(Number);
  for (let i = 0; i < Math.max(pa.length, pb.length); i++) {
    const diff = (pa[i] ?? 0) - (pb[i] ?? 0);
    if (diff !== 0) return diff;
  }
  return 0;
}).at(-1);
```

Extract as a shared utility `latestVersion(versions: string[]): string | undefined` in a shared location.

### 5.3 — Single demux listener for sync

**File:** `src/stores/middlewares/sync.ts`

Replace per-store `ipcRenderer.on("sync:push", ...)` with a single listener registered once:

```ts
const listeners = new Map<string, (state: string) => void>();

function ensureListener() {
  if (listeners.size > 0) return; // already registered
  window.electronSync.onReceive((storeName, state) => {
    listeners.get(storeName)?.(state);
  });
}
```

Each store's `sync()` middleware calls `listeners.set(name, applyRemoteState)` and `ensureListener()`.

### 5.4 — Update electron-vite config

**File:** `electron-vite.config.ts`

Remove `externalizeDepsPlugin` import and usage. The default behavior in electron-vite 5.0+ handles externalization automatically.

```ts
import react from "@vitejs/plugin-react";
import { defineConfig } from "electron-vite";

export default defineConfig({
  main: {},
  preload: {},
  renderer: {
    plugins: [react()],
  },
});
```

### 5.5 — Merge address bar `useStore` calls

**File:** `src/renderer/pages/browser/_components/address-bar-connected.tsx:20-21`

```ts
const { activeTabId, activeProfileId } = useStore(
  tabStore,
  useShallow(s => ({ activeTabId: s.activeTabId, activeProfileId: s.activeProfileId })),
);
```

### 5.6 — Targeted `updateTab`/`closeTab`

**File:** `src/stores/profile-store.ts:121-138`

Instead of mapping all profiles, find the owning profile first:

```ts
updateTab: (tabId, partial) => {
  set((s) => ({
    profiles: s.profiles.map((p) => {
      const tabIdx = p.tabs.findIndex((t) => t.id === tabId);
      if (tabIdx === -1) return p; // unchanged reference
      return { ...p, tabs: p.tabs.map((t) => (t.id === tabId ? { ...t, ...partial } : t)) };
    }),
  }));
},
```

Profiles that don't own the tab keep their reference identity — React skips re-rendering them.

### 5.7 — `chrome.scripting` stub

**File:** Existing service worker shim or new `sw-extras.ts`

Add a minimal stub for `chrome.scripting.executeScript`:

```ts
chrome.scripting = {
  executeScript: async ({ target, func, args }) => {
    // Route through IPC to webContents.executeJavaScript on target tab
    // Or return empty results for unsupported cases
    return [];
  },
};
```

Prevents MV3 extensions using `chrome.scripting` from throwing.

### 5.8 — ExtensionLoader retry cap

**File:** `src/main/profile/extension-loader.ts:17-26`

Add a retry counter (max 3). After 3 failed load attempts, stop retrying and log the error:

```ts
private retryCount = 0;
private static readonly MAX_RETRIES = 3;

async ensureLoaded(): Promise<void> {
  if (this.loadPromise) return this.loadPromise;
  if (this.retryCount >= ExtensionLoader.MAX_RETRIES) return;

  this.loadPromise = this.loadAll().catch((err) => {
    this.loadPromise = null;
    this.retryCount++;
    console.error(`Extension load failed (attempt ${this.retryCount}):`, err);
  });
  return this.loadPromise;
}
```

### 5.9 — Dev extension auto-install guard

**File:** `src/main/pane.ts:66-85`

Gate behind environment variable:

```ts
if (process.env.PANE_DEV_EXTENSIONS === "1") {
  // install Dark Reader, 1Password, NordPass
}
```

Does not fire on every `bun run dev` unless explicitly opted in.
