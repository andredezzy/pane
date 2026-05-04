# tRPC IPC Migration

Replace raw `contextBridge`/`window.pane` IPC with tRPC for end-to-end type-safe communication between renderer and main process. Also replace the custom store sync system (`window.electronSync`) with tRPC subscriptions.

## Decisions

- **Option B** — full migration: both commands (`window.pane`) and store sync (`window.electronSync`) move to tRPC
- Extension package (`@pane/electron-chrome-extensions`) IPC is untouched — separate concern, separate package
- Vanilla tRPC client (no React Query) — app uses zustand for state, no second state layer needed
- Router per domain — each domain gets its own router file, composed at root
- **tRPC v11** with `trpc-electron` (mat-sz fork) — supports async generator subscriptions
- **Local `createIPCHandler` wrapper** — `trpc-electron` only accepts `BrowserWindow`, but our app uses `BaseWindow` + `WebContentsView`. The wrapper accepts `WebContents` directly. Preload (`exposeElectronTRPC`) and renderer (`ipcLink`) are used from the package unchanged.

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
- `@trpc/server@^11` — main process router
- `@trpc/client@^11` — renderer client
- `trpc-electron` (mat-sz fork) — IPC transport (`exposeElectronTRPC` for preload, `ipcLink` for renderer)

Already present: `zod` (v4), used for input validation schemas.

Note: We do NOT use `trpc-electron`'s `createIPCHandler` because it only accepts `BrowserWindow`. We write a local wrapper (~80 lines) that accepts `WebContents` directly.

## Files Created

| File | Purpose |
|---|---|
| `src/main/trpc/trpc.ts` | tRPC instance, context type, exports `router` and `procedure` |
| `src/main/trpc/router.ts` | Root router composition, exports `AppRouter` type |
| `src/main/trpc/ipc.ts` | Local `createIPCHandler` that accepts `WebContents` instead of `BrowserWindow` |
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

## Local IPC Handler

`trpc-electron`'s `createIPCHandler` only accepts `BrowserWindow`. Our app uses `BaseWindow` + `WebContentsView`. We write a local handler that accepts `WebContents` directly, compatible with `trpc-electron`'s preload/renderer protocol.

```ts
// src/main/trpc/ipc.ts
import { ipcMain, type WebContents } from "electron";
import { callTRPCProcedure, type AnyTRPCRouter } from "@trpc/server";
import { ELECTRON_TRPC_CHANNEL } from "trpc-electron/main";

interface IPCHandlerOptions<TRouter extends AnyTRPCRouter> {
  router: TRouter;
  webContents: WebContents;
  createContext: () => any;
}

export function createIPCHandler<TRouter extends AnyTRPCRouter>({
  router,
  webContents,
  createContext,
}: IPCHandlerOptions<TRouter>) {
  // Listen on ELECTRON_TRPC_CHANNEL ("trpc-electron") for requests from renderer
  // Route to callTRPCProcedure (internal tRPC API, same as trpc-electron uses)
  // Handle subscriptions with abort controllers
  // Clean up subscriptions on webContents destroyed / did-start-navigation
  // Message protocol must match trpc-electron's exposeElectronTRPC format
}
```

`callTRPCProcedure` is a low-level internal tRPC API — but `trpc-electron` itself uses it for the same purpose, so it's the correct approach for a custom transport handler.

Implementation references `trpc-electron`'s `IPCHandler` class (~80 lines). Key responsibilities:
- Listen on `ELECTRON_TRPC_CHANNEL` (`"trpc-electron"`) — same channel `exposeElectronTRPC` uses
- Parse incoming messages, route to `callTRPCProcedure`
- For subscriptions: iterate async generators, send each yield back via `webContents.send`
- Track active subscriptions with `AbortController` per subscription
- On `webContents` `destroyed` event: abort all subscriptions
- On `did-start-navigation` (non-same-document): abort all subscriptions

Message protocol (must match `trpc-electron`'s preload/renderer):
- **Renderer → Main** (`ipcRenderer.send("trpc-electron", message)`):
  - `{ method: "request", operation: Operation }` — query/mutation/subscription start
  - `{ method: "subscription.stop", id: number }` — subscription teardown
- **Main → Renderer** (`event.sender.send("trpc-electron", response)`):
  - `TRPCResponseMessage` from `@trpc/server/rpc` after `transformTRPCResponse()`
- **Preload global**: `window.electronTRPC: { sendMessage(msg): void, onMessage(cb): void }`

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
    let trpc: Awaited<typeof import("../../renderer/trpc")>["trpc"] | null = null;

    import("../../renderer/trpc").then((mod) => {
      trpc = mod.trpc;

      trpc!.stores.sync.subscribe(
        { name },
        {
          onData(serialized: string) {
            applyingRemote = true;
            set((state) => ({ ...state, ...JSON.parse(serialized) }));
            applyingRemote = false;
          },
        },
      );
    });

    const syncedSet: typeof set = (updater, replace) => {
      set(updater, replace as never);
      if (applyingRemote || !trpc) return;
      trpc.stores.push.mutate({ name, state: serializeState(get()) });
    };

    return storeCreator(syncedSet, get, api);
  };
}
```

The dynamic import resolves the tRPC client internally — no globals, no setters. The `isRenderer()` guard ensures the import never executes in main. The client type is inferred via `Awaited<typeof import(...)>["trpc"]` — full type safety without manual type declarations. There is a ~1 microtask timing gap before `trpc` resolves; `syncedSet` skips push during that window, and the subscription's initial yield reconciles immediately after.

Note: The vanilla tRPC client's `.subscribe()` returns an Observable, consumed via `{ onData }` callback — not an `AsyncIterable`. The server-side subscription still uses `async function*`, but the client transport layer wraps it in an Observable.

Store files remain unchanged — they still wrap with `sync(creator, { name })`.

## Preload

```ts
// src/preload/index.ts
import { injectBrowserAction } from "@pane/electron-chrome-extensions/browser-action";
import { exposeElectronTRPC } from "trpc-electron/main";

injectBrowserAction();
exposeElectronTRPC();
```

No more `contextBridge.exposeInMainWorld("pane", ...)` or `electronSync`. The `ElectronSync` and `PaneAPI` type exports are also removed.

## Renderer Client

```ts
// src/renderer/trpc.ts
import { createTRPCClient } from "@trpc/client";
import { ipcLink } from "trpc-electron/renderer";
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
import { createIPCHandler } from "./trpc/ipc";
import { appRouter } from "./trpc/router";

// Replace IpcRouter + StoreSync with:
createIPCHandler({
  router: appRouter,
  webContents: win.uiView.webContents,
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

2. **Subscription cleanup on window close**: The async generator's `finally` block unsubscribes from zustand. Our local IPC handler must abort subscriptions on `webContents` `destroyed` and `did-start-navigation` events — triggering the generator's `finally` block. Reference `trpc-electron`'s cleanup logic.

3. **Double serialization**: `serializeState` returns a JSON string. tRPC also serializes its transport payload as JSON. The `sync` subscription yields a string, and `push` receives a string — so the state travels as a JSON string inside the tRPC JSON envelope. The `onData` callback receives the string as-is (tRPC deserializes the envelope, the string payload comes through intact). On the renderer side, `JSON.parse(serialized)` recovers the object.

4. **Import graph: sync middleware in shared context**: The sync middleware uses a dynamic `import("../../renderer/trpc")` guarded by `isRenderer()`. In the renderer Vite build, this resolves within the same module graph. In the main esbuild, the import is never executed. Verify the main build doesn't warn about the unresolved dynamic import — if it does, add a `/* @vite-ignore */` comment.

5. **Local `createIPCHandler` for BaseWindow**: `trpc-electron`'s `createIPCHandler` only accepts `BrowserWindow`. Our local wrapper accepts `WebContents` directly. It must handle: IPC message routing to tRPC, subscription lifecycle (setup/teardown), cleanup on `webContents.on("destroyed")` and `did-start-navigation`. Reference `trpc-electron`'s `IPCHandler` source for the message protocol — our wrapper must be compatible with the same preload/renderer code.

6. **`findProfileForActiveTab` pattern**: Several tab procedures (`goBack`, `goForward`, `reload`, `showActive`) need to find the active tab's profile. The current `IpcRouter` uses a private helper that reads `tabStore`. The tRPC context includes `stores` — the router can read `tabStore` state the same way. Extract a shared helper function used by these procedures.
