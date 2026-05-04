# electron-trpc Migration

Replace raw `contextBridge`/`window.pane` IPC with electron-trpc for end-to-end type-safe communication between renderer and main process. Also replace the custom store sync system (`window.electronSync`) with tRPC subscriptions.

## Decisions

- **Option B** — full migration: both commands (`window.pane`) and store sync (`window.electronSync`) move to tRPC
- Extension package (`@pane/electron-chrome-extensions`) IPC is untouched — separate concern, separate package
- Vanilla tRPC client (no React Query) — app uses zustand for state, no second state layer needed
- Router per domain — each domain gets its own router file, composed at root

## Architecture

```
renderer                    preload                 main
─────────                   ───────                 ────
trpc client  ──ipcRenderer──  exposeElectronTRPC  ──ipcMain──  tRPC router
                                                                 ├── tabs
                                                                 ├── profiles
                                                                 ├── settings
                                                                 ├── extensions
                                                                 ├── cws
                                                                 └── stores
                                                                      ├── push (mutation)
                                                                      └── sync (subscription)
```

## Dependencies

New packages to install in `apps/desktop`:
- `@trpc/server` — main process router
- `@trpc/client` — renderer client
- `electron-trpc` — IPC transport (exposeElectronTRPC, ipcLink, createIPCHandler)

Already present: `zod` (v4), used for input validation schemas.

## Files Created

| File | Purpose |
|---|---|
| `src/main/trpc/trpc.ts` | tRPC instance, context type, exports `router` and `procedure` |
| `src/main/trpc/router.ts` | Root router composition, exports `AppRouter` type |
| `src/main/trpc/routers/tabs.ts` | 9 tab procedures (open, close, switch, navigate, goBack, goForward, reload, hideAll, showActive) |
| `src/main/trpc/routers/profiles.ts` | 1 procedure (activate) |
| `src/main/trpc/routers/settings.ts` | 1 procedure (detectBrowser) |
| `src/main/trpc/routers/extensions.ts` | 1 procedure (list) |
| `src/main/trpc/routers/cws.ts` | 3 procedures (install, uninstall, installed) |
| `src/main/trpc/routers/stores.ts` | push mutation + sync subscription + storeChanges async generator |
| `src/renderer/trpc.ts` | Typed tRPC client singleton |

## Files Modified

| File | Change |
|---|---|
| `src/preload/index.ts` | Remove `window.pane` and `window.electronSync` bridges. Add `exposeElectronTRPC()`. Keep `injectBrowserAction()`. |
| `src/main/index.ts` | Remove `IpcRouter` and `StoreSync` usage. Add `createIPCHandler` with router, window, and context. |
| `src/stores/middlewares/sync.ts` | Rewrite to use tRPC client (`trpc.stores.push.mutate` + `trpc.stores.sync.subscribe`) instead of `window.electronSync`. |
| `src/renderer/components/sidebar/sidebar-connected.tsx` | Replace `window.pane?.x` calls with `trpc.x.y.mutate()` |
| `src/renderer/pages/browser/_components/address-bar-connected.tsx` | Replace `window.pane?.x` calls with `trpc.x.y.mutate()` |
| `src/renderer/pages/settings/index.tsx` | Replace `window.pane?.x` calls with `trpc.x.y.mutate()` |
| `src/renderer/main.tsx` | Remove `declare global { interface Window }` augmentation |

## Files Deleted

| File | Reason |
|---|---|
| `src/main/ipc.ts` | Replaced by tRPC routers |
| `src/stores/middlewares/store-sync.ts` | Replaced by tRPC stores router (sync subscription + push mutation) |

## tRPC Instance & Context

```ts
// src/main/trpc/trpc.ts
import { initTRPC } from "@trpc/server";
import type { StoreApi } from "zustand/vanilla";
import type { Pane } from "../pane";

type StoreName =
  | "profile-store"
  | "tab-store"
  | "navigation-store"
  | "settings-store"
  | "extension-store";

interface Context {
  pane: Pane;
  stores: Record<StoreName, StoreApi<object>>;
}

const t = initTRPC.context<Context>().create();

export const router = t.router;
export const procedure = t.procedure;
export type { Context, StoreName };
```

`Pane` and `stores` are injected as context — every procedure accesses them via `ctx`. No globals, no hidden dependencies.

## Root Router

```ts
// src/main/trpc/router.ts
import { router } from "./trpc";
import { tabsRouter } from "./routers/tabs";
import { profilesRouter } from "./routers/profiles";
import { settingsRouter } from "./routers/settings";
import { extensionsRouter } from "./routers/extensions";
import { cwsRouter } from "./routers/cws";
import { storesRouter } from "./routers/stores";

export const appRouter = router({
  tabs: tabsRouter,
  profiles: profilesRouter,
  settings: settingsRouter,
  extensions: extensionsRouter,
  cws: cwsRouter,
  stores: storesRouter,
});

export type AppRouter = typeof appRouter;
```

## Domain Routers

Each router maps 1:1 to the current IPC channels.

### tabs

| Procedure | Type | Input | Logic |
|---|---|---|---|
| `open` | mutation | `{ profileId: string, url?: string }` | `pane.hideAllTabs()` then `pane.getOrCreateProfile(profileId).tabs.open(url)` |
| `close` | mutation | `{ tabId: string }` | `pane.getProfileForTab(tabId)?.tabs.close(tabId)` |
| `switch` | mutation | `{ tabId: string }` | `pane.hideAllTabs()` then `pane.getProfileForTab(tabId)?.tabs.activate(tabId)` |
| `navigate` | mutation | `{ url: string }` | Find active tab profile, call `tabs.navigate(url)` |
| `goBack` | mutation | (none) | Find active tab profile, call `tabs.goBack()` |
| `goForward` | mutation | (none) | Find active tab profile, call `tabs.goForward()` |
| `reload` | mutation | (none) | Find active tab profile, call `tabs.reload()` |
| `hideAll` | mutation | (none) | `pane.hideAllTabs()` |
| `showActive` | mutation | (none) | Find active tab profile, call `tabs.showActive()` |

`goBack`, `goForward`, `reload`, `showActive` need access to `tabStore` to find the active profile. The current `IpcRouter` does this via `findProfileForActiveTab()`. The tRPC context includes `stores` which gives access to `tabStore` state for the same lookup.

### profiles

| Procedure | Type | Input | Logic |
|---|---|---|---|
| `activate` | mutation | `{ profileId: string }` | `pane.getOrCreateProfile(profileId).extensions.ensureLoaded()` |

### settings

| Procedure | Type | Input | Logic |
|---|---|---|---|
| `detectBrowser` | mutation | (none) | Detect browser path, save to settingsStore, return path or undefined |

### extensions

| Procedure | Type | Input | Logic |
|---|---|---|---|
| `list` | query | `{ profileId: string }` | Get profile, return loaded extensions `[{ id, name, version }]` |

### cws

| Procedure | Type | Input | Logic |
|---|---|---|---|
| `install` | mutation | `{ extensionId: string }` | `pane.extensions.install(extensionId)` |
| `uninstall` | mutation | `{ extensionId: string }` | `pane.extensions.uninstall(extensionId)` |
| `installed` | query | (none) | `pane.extensions.getInstalled()` |

## Store Sync

Replaces both `StoreSync` (main) and the `sync` middleware (renderer).

### Main: stores router

```ts
// src/main/trpc/routers/stores.ts

const StoreNameSchema = z.enum([
  "profile-store",
  "tab-store",
  "navigation-store",
  "settings-store",
  "extension-store",
]);

export const storesRouter = router({
  push: procedure
    .input(z.object({ name: StoreNameSchema, state: z.string() }))
    .mutation(({ input, ctx }) => {
      const store = ctx.stores[input.name];
      const partial = JSON.parse(input.state);
      store.setState((prev) => ({ ...prev, ...partial }));
    }),

  sync: procedure
    .input(z.object({ name: StoreNameSchema }))
    .subscription(async function* ({ input, ctx }) {
      const store = ctx.stores[input.name];
      yield serializeState(store.getState());
      yield* storeChanges(store);
    }),
});
```

### storeChanges async generator

Converts a zustand store subscription into an async iterable with 16ms debounce (matching current `StoreSync.scheduleBroadcast`):

```ts
async function* storeChanges(store: StoreApi<object>) {
  let resolve: (() => void) | null = null;
  let pending = false;
  let timer: ReturnType<typeof setTimeout> | null = null;

  const unsubscribe = store.subscribe(() => {
    pending = true;
    if (timer) clearTimeout(timer);
    timer = setTimeout(() => {
      timer = null;
      if (resolve) {
        resolve();
        resolve = null;
      }
    }, 16);
  });

  try {
    while (true) {
      if (!pending) {
        await new Promise<void>((r) => { resolve = r; });
      }
      pending = false;
      yield serializeState(store.getState());
    }
  } finally {
    unsubscribe();
    if (timer) clearTimeout(timer);
  }
}
```

The `finally` block ensures the zustand subscription is cleaned up when the tRPC subscription disconnects (window close, navigation, etc.).

### Renderer: sync middleware rewrite

```ts
// src/stores/middlewares/sync.ts
import type { StateCreator } from "zustand/vanilla";
import { serializeState } from "./serialize";

type TrpcClient = {
  stores: {
    push: { mutate: (input: { name: string; state: string }) => Promise<void> };
    sync: { subscribe: (input: { name: string }) => AsyncIterable<string> };
  };
};

let trpcClient: TrpcClient | null = null;

export function setTrpcClient(client: TrpcClient) {
  trpcClient = client;
}

function isRenderer(): boolean {
  return typeof window !== "undefined";
}

export function sync<TState>(
  storeCreator: StateCreator<TState, [], []>,
  config: { name: string },
): StateCreator<TState, [], []> {
  const { name } = config;

  return (set, get, api) => {
    if (!isRenderer()) {
      return storeCreator(set, get, api);
    }

    let applyingRemote = false;

    const syncedSet: typeof set = (updater, replace) => {
      set(updater, replace as never);
      if (applyingRemote) return;
      trpcClient?.stores.push.mutate({ name, state: serializeState(get()) });
    };

    // Subscribe to main process state changes
    if (trpcClient) {
      (async () => {
        for await (const serialized of trpcClient.stores.sync.subscribe({ name })) {
          const partial = JSON.parse(serialized as string);
          applyingRemote = true;
          set((state) => ({ ...state, ...partial }));
          applyingRemote = false;
        }
      })();
    }

    return storeCreator(syncedSet, get, api);
  };
}
```

Note: The sync middleware cannot import `trpc` directly from `renderer/trpc.ts` because the store modules are shared between main and renderer (same files, different runtime contexts). The `isRenderer()` guard prevents main process from running sync logic. The renderer initializes the client reference via `setTrpcClient` at app startup before stores are created.

**Alternative**: If the import graph can be structured so that `renderer/trpc.ts` is only ever bundled for renderer, a direct import works and `setTrpcClient` is unnecessary. This depends on the electron-vite build configuration — verify during implementation.

## Preload

```ts
// src/preload/index.ts
import { injectBrowserAction } from "@pane/electron-chrome-extensions/browser-action";
import { exposeElectronTRPC } from "electron-trpc/main";

injectBrowserAction();
exposeElectronTRPC();
```

No more `contextBridge.exposeInMainWorld("pane", ...)` or `electronSync`. The `ElectronSync` and `PaneAPI` type exports are also removed.

## Renderer Client

```ts
// src/renderer/trpc.ts
import { createTRPCClient } from "@trpc/client";
import { ipcLink } from "electron-trpc/renderer";
import type { AppRouter } from "../main/trpc/router";

export const trpc = createTRPCClient<AppRouter>({
  links: [ipcLink()],
});
```

## Renderer Usage Migration

All `window.pane?.x.y(args)` calls become `trpc.x.y.mutate(args)`:

| Before | After |
|---|---|
| `window.pane?.tabs.open(profileId)` | `trpc.tabs.open.mutate({ profileId })` |
| `window.pane?.tabs.close(tabId)` | `trpc.tabs.close.mutate({ tabId })` |
| `window.pane?.tabs.switch(tabId)` | `trpc.tabs.switch.mutate({ tabId })` |
| `window.pane?.tabs.navigate(url)` | `trpc.tabs.navigate.mutate({ url })` |
| `window.pane?.tabs.goBack()` | `trpc.tabs.goBack.mutate()` |
| `window.pane?.tabs.goForward()` | `trpc.tabs.goForward.mutate()` |
| `window.pane?.tabs.reload()` | `trpc.tabs.reload.mutate()` |
| `window.pane?.tabs.hideAll()` | `trpc.tabs.hideAll.mutate()` |
| `window.pane?.tabs.showActive()` | `trpc.tabs.showActive.mutate()` |
| `window.pane?.profiles.activate(profileId)` | `trpc.profiles.activate.mutate({ profileId })` |
| `window.pane?.settings.detectBrowser()` | `trpc.settings.detectBrowser.mutate()` |
| `window.pane?.extensions.list(profileId)` | `trpc.extensions.list.query({ profileId })` |
| `window.pane?.cws.install(extensionId)` | `trpc.cws.install.mutate({ extensionId })` |
| `window.pane?.cws.uninstall(extensionId)` | `trpc.cws.uninstall.mutate({ extensionId })` |
| `window.pane?.cws.installed()` | `trpc.cws.installed.query()` |

## Main Process Wiring

```ts
// src/main/index.ts changes
import { createIPCHandler } from "electron-trpc/main";
import { appRouter } from "./trpc/router";

// Replace IpcRouter + StoreSync with:
createIPCHandler({
  router: appRouter,
  windows: [win.mainWindow],
  createContext: () => ({
    pane,
    stores: {
      "profile-store": profileStore,
      "tab-store": tabStore,
      "navigation-store": navigationStore,
      "settings-store": settingsStore,
      "extension-store": extensionStore,
    },
  }),
});
```

## Edge Cases & Risks

1. **Subscription startup race**: The renderer might call `syncedSet` (which calls `push.mutate`) before the `sync` subscription has yielded initial state. This is safe — mutations and subscriptions are independent channels. The initial state from the subscription will arrive and overwrite any stale renderer state.

2. **Subscription cleanup on window close**: The async generator's `finally` block unsubscribes from zustand. electron-trpc must tear down the subscription when the IPC channel disconnects. Verify this during implementation by checking electron-trpc source.

3. **Double serialization**: `serializeState` returns a JSON string. tRPC also serializes its transport payload as JSON. The `sync` subscription yields a string, and `push` receives a string — so the state travels as a JSON string inside the tRPC JSON envelope. On the renderer side, `JSON.parse(serialized)` recovers the object. No double-encoding issue since the string is an opaque payload to tRPC.

4. **Import graph: sync middleware in shared context**: The sync middleware (`stores/middlewares/sync.ts`) is imported by store files that run in both main and renderer. It cannot directly import `renderer/trpc.ts`. The `isRenderer()` guard prevents main from running sync logic. For the renderer, either use `setTrpcClient` initialization or verify that electron-vite tree-shakes the renderer import correctly.

5. **electron-trpc + BaseWindow**: The current app uses `BaseWindow` + `WebContentsView`, not `BrowserWindow`. Verify that `createIPCHandler({ windows: [...] })` supports `BaseWindow`, or if we need to pass the `WebContentsView`'s webContents directly. Check electron-trpc API.

6. **`findProfileForActiveTab` pattern**: Several tab procedures (`goBack`, `goForward`, `reload`, `showActive`) need to find the active tab's profile. The current `IpcRouter` uses a private helper that reads `tabStore`. The tRPC context includes `stores` — the router can read `tabStore` state the same way. Extract a shared helper function used by these procedures.
