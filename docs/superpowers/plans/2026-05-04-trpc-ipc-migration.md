# tRPC IPC Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace raw `contextBridge`/`window.pane` IPC with tRPC v11 for end-to-end type-safe communication between renderer and main process, including store sync.

**Architecture:** tRPC router in main process with domain routers (tabs, profiles, settings, extensions, cws, stores). Local IPC handler for `WebContents` compatibility. Renderer uses vanilla tRPC client. Store sync via tRPC subscription (async generator server-side, observable client-side).

**Tech Stack:** tRPC v11, trpc-electron (mat-sz), zustand, zod v4, electron-vite

**Spec:** `docs/superpowers/specs/2026-05-04-electron-trpc-migration-design.md`

---

### Task 1: Install dependencies

**Files:**
- Modify: `apps/desktop/package.json`

- [ ] **Step 1: Install tRPC and trpc-electron packages**

```bash
cd /Users/andrevictor/www/pane && npm install --workspace=apps/desktop @trpc/server@^11 @trpc/client@^11 trpc-electron
```

- [ ] **Step 2: Verify packages installed**

```bash
cd /Users/andrevictor/www/pane && node -e "require('@trpc/server'); require('@trpc/client'); console.log('OK')"
```

Expected: `OK`

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/package.json package-lock.json && git commit -m "chore: add trpc and trpc-electron dependencies"
```

---

### Task 2: Create tRPC instance and context

**Files:**
- Create: `apps/desktop/src/main/trpc/trpc.ts`

- [ ] **Step 1: Create the tRPC instance**

```ts
// apps/desktop/src/main/trpc/trpc.ts
import { initTRPC } from "@trpc/server";
import type { StoreApi } from "zustand/vanilla";
import type { Pane } from "../pane";

export type StoreName =
	| "profile-store"
	| "tab-store"
	| "navigation-store"
	| "settings-store"
	| "extension-store";

export interface Context {
	pane: Pane;
	stores: Record<StoreName, StoreApi<object>>;
}

const t = initTRPC.context<Context>().create({ isServer: true });

export const router = t.router;
export const procedure = t.procedure;
```

- [ ] **Step 2: Verify typecheck**

```bash
cd /Users/andrevictor/www/pane && npx turbo run typecheck --filter=@pane/desktop
```

Expected: success

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/src/main/trpc/trpc.ts && git commit -m "feat(trpc): add tRPC instance and context type"
```

---

### Task 3: Create local IPC handler for WebContents

This is the most complex piece. It replicates `trpc-electron`'s `IPCHandler` but accepts `WebContents` directly instead of `BrowserWindow`.

**Files:**
- Create: `apps/desktop/src/main/trpc/ipc.ts`

- [ ] **Step 1: Create the IPC handler**

```ts
// apps/desktop/src/main/trpc/ipc.ts
import {
	callTRPCProcedure,
	getErrorShape,
	getTRPCErrorFromUnknown,
	isTrackedEnvelope,
	transformTRPCResponse,
	TRPCError,
} from "@trpc/server";
import type { AnyTRPCRouter, inferRouterContext } from "@trpc/server";
import { isObservable, observableToAsyncIterable } from "@trpc/server/observable";
import type { TRPCResponseMessage, TRPCResultMessage } from "@trpc/server/rpc";
import { ipcMain } from "electron";
import type { IpcMainEvent, WebContents } from "electron";

import { ELECTRON_TRPC_CHANNEL } from "trpc-electron/main";

import type { Context } from "./trpc";

type ETRPCRequest =
	| { method: "request"; operation: { id: number; type: "query" | "mutation" | "subscription"; path: string; input?: unknown } }
	| { method: "subscription.stop"; id: number };

function isAsyncIterable<T>(value: unknown): value is AsyncIterable<T> {
	return (
		typeof Symbol === "function" &&
		!!Symbol.asyncIterator &&
		!!value &&
		typeof value === "object" &&
		!Array.isArray(value) &&
		Symbol.asyncIterator in value
	);
}

function getInternalId(event: IpcMainEvent, request: ETRPCRequest): string {
	const messageId =
		request.method === "request" ? request.operation.id : request.id;
	return `${event.sender.id}-${event.senderFrame.routingId}:${messageId}`;
}

interface IPCHandlerOptions<TRouter extends AnyTRPCRouter> {
	router: TRouter;
	webContents: WebContents;
	createContext: () => inferRouterContext<TRouter>;
}

export function createIPCHandler<TRouter extends AnyTRPCRouter>({
	router,
	webContents,
	createContext,
}: IPCHandlerOptions<TRouter>): () => void {
	const subscriptions = new Map<string, AbortController>();

	const webContentsId = webContents.id;

	function cleanUpSubscriptions(frameRoutingId?: number) {
		const prefix = `${webContentsId}-${frameRoutingId ?? ""}`;
		for (const [key, sub] of subscriptions.entries()) {
			if (key.startsWith(prefix)) {
				sub.abort();
				subscriptions.delete(key);
			}
		}
	}

	function respond(event: IpcMainEvent, response: TRPCResponseMessage) {
		if (event.sender.isDestroyed()) return;
		event.reply(
			ELECTRON_TRPC_CHANNEL,
			transformTRPCResponse(router._def._config, response),
		);
	}

	async function handleMessage(event: IpcMainEvent, message: ETRPCRequest) {
		const internalId = getInternalId(event, message);

		if (message.method === "subscription.stop") {
			subscriptions.get(internalId)?.abort();
			return;
		}

		const { type, input: serializedInput, path, id } = message.operation;
		const input = serializedInput
			? router._def._config.transformer.input.deserialize(serializedInput)
			: undefined;

		const ctx = createContext();

		try {
			const abortController = new AbortController();
			const result = await callTRPCProcedure({
				ctx,
				path,
				procedures: router._def.procedures,
				getRawInput: async () => input,
				type,
				signal: abortController.signal,
			});

			const isIterableResult =
				isAsyncIterable(result) || isObservable(result);

			if (type !== "subscription") {
				if (isIterableResult) {
					throw new TRPCError({
						code: "UNSUPPORTED_MEDIA_TYPE",
						message: `Cannot return an async iterable or observable from a ${type} procedure.`,
					});
				}

				respond(event, {
					id,
					result: { type: "data", data: result },
				});
				return;
			}

			if (!isIterableResult) {
				throw new TRPCError({
					message: `Subscription ${path} did not return an observable or AsyncGenerator`,
					code: "INTERNAL_SERVER_ERROR",
				});
			}

			if (subscriptions.has(internalId)) {
				throw new TRPCError({
					message: `Duplicate id ${internalId}`,
					code: "BAD_REQUEST",
				});
			}

			const iterable = isObservable(result)
				? observableToAsyncIterable(result, abortController.signal)
				: result;

			const iterator = iterable[Symbol.asyncIterator]();

			(async () => {
				try {
					const abortPromise = new Promise<"abort">((resolve) => {
						abortController.signal.onabort = () => resolve("abort");
					});

					while (true) {
						const next = await Promise.race([
							iterator.next().catch(getTRPCErrorFromUnknown),
							abortPromise,
						]);

						if (next === "abort") {
							await iterator.return?.();
							break;
						}

						if (next instanceof Error) {
							const error = getTRPCErrorFromUnknown(next);
							respond(event, {
								id,
								error: getErrorShape({
									config: router._def._config,
									error,
									type,
									path,
									input,
									ctx,
								}),
							});
							break;
						}

						if (next.done) {
							break;
						}

						let result: TRPCResultMessage<unknown>["result"] = {
							type: "data",
							data: next.value,
						};

						if (isTrackedEnvelope(next.value)) {
							const [trackId, data] = next.value;
							result = { type: "data", id: trackId, data: { id: trackId, data } };
						}

						respond(event, { id, result });
					}

					respond(event, { id, result: { type: "stopped" } });
					subscriptions.delete(internalId);
				} catch (cause) {
					const error = getTRPCErrorFromUnknown(cause);
					respond(event, {
						id,
						error: getErrorShape({
							config: router._def._config,
							error,
							type,
							path,
							input,
							ctx,
						}),
					});
					abortController.abort();
				}
			})();

			respond(event, { id, result: { type: "started" } });
			subscriptions.set(internalId, abortController);
		} catch (cause) {
			const error = getTRPCErrorFromUnknown(cause);
			respond(event, {
				id,
				error: getErrorShape({
					config: router._def._config,
					error,
					type,
					path,
					input,
					ctx,
				}),
			});
		}
	}

	const listener = (event: IpcMainEvent, request: ETRPCRequest) => {
		if (event.sender.id !== webContentsId) return;
		handleMessage(event, request);
	};

	ipcMain.on(ELECTRON_TRPC_CHANNEL, listener);

	webContents.on("did-start-navigation", ({ isSameDocument, frame }) => {
		if (!isSameDocument) {
			cleanUpSubscriptions(frame.routingId);
		}
	});

	webContents.on("destroyed", () => {
		cleanUpSubscriptions();
		ipcMain.removeListener(ELECTRON_TRPC_CHANNEL, listener);
	});

	return () => {
		cleanUpSubscriptions();
		ipcMain.removeListener(ELECTRON_TRPC_CHANNEL, listener);
	};
}
```

Key difference from `trpc-electron`'s `IPCHandler`: accepts `WebContents` directly, filters events by `sender.id` to only process messages from our UI view, and returns a cleanup function.

- [ ] **Step 2: Verify typecheck**

```bash
cd /Users/andrevictor/www/pane && npx turbo run typecheck --filter=@pane/desktop
```

Expected: success. If there are import issues with `trpc-electron/main` exports (e.g., `ELECTRON_TRPC_CHANNEL` not exported), check the package's actual exports and adjust the import. The constant value is `"trpc-electron"` — can be inlined if needed.

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/src/main/trpc/ipc.ts && git commit -m "feat(trpc): add local IPC handler for WebContents"
```

---

### Task 4: Create stores router with sync subscription

**Files:**
- Create: `apps/desktop/src/main/trpc/routers/stores.ts`

- [ ] **Step 1: Create the stores router**

```ts
// apps/desktop/src/main/trpc/routers/stores.ts
import { z } from "zod/v4";
import type { StoreApi } from "zustand/vanilla";

import { serializeState } from "../../../stores/middlewares/serialize";
import { procedure, router } from "../trpc";

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
				await new Promise<void>((r) => {
					resolve = r;
				});
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

- [ ] **Step 2: Verify typecheck**

```bash
cd /Users/andrevictor/www/pane && npx turbo run typecheck --filter=@pane/desktop
```

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/src/main/trpc/routers/stores.ts && git commit -m "feat(trpc): add stores router with sync subscription"
```

---

### Task 5: Create domain routers (tabs, profiles, settings, extensions, cws)

**Files:**
- Create: `apps/desktop/src/main/trpc/routers/tabs.ts`
- Create: `apps/desktop/src/main/trpc/routers/profiles.ts`
- Create: `apps/desktop/src/main/trpc/routers/settings.ts`
- Create: `apps/desktop/src/main/trpc/routers/extensions.ts`
- Create: `apps/desktop/src/main/trpc/routers/cws.ts`

- [ ] **Step 1: Create tabs router**

```ts
// apps/desktop/src/main/trpc/routers/tabs.ts
import { z } from "zod/v4";

import { tabStore } from "../../../stores/tab-store";
import { procedure, router } from "../trpc";

function findActiveProfile(ctx: { pane: import("../../pane").Pane }) {
	const { activeProfileId } = tabStore.getState();
	return activeProfileId ? ctx.pane.getProfile(activeProfileId) : undefined;
}

export const tabsRouter = router({
	open: procedure
		.input(z.object({ profileId: z.string(), url: z.string().optional() }))
		.mutation(({ input, ctx }) => {
			ctx.pane.hideAllTabs();
			ctx.pane.getOrCreateProfile(input.profileId).tabs.open(input.url);
		}),

	close: procedure
		.input(z.object({ tabId: z.string() }))
		.mutation(({ input, ctx }) => {
			ctx.pane.getProfileForTab(input.tabId)?.tabs.close(input.tabId);
		}),

	switch: procedure
		.input(z.object({ tabId: z.string() }))
		.mutation(({ input, ctx }) => {
			ctx.pane.hideAllTabs();
			ctx.pane.getProfileForTab(input.tabId)?.tabs.activate(input.tabId);
		}),

	navigate: procedure
		.input(z.object({ url: z.string() }))
		.mutation(({ input, ctx }) => {
			findActiveProfile(ctx)?.tabs.navigate(input.url);
		}),

	goBack: procedure.mutation(({ ctx }) => {
		findActiveProfile(ctx)?.tabs.goBack();
	}),

	goForward: procedure.mutation(({ ctx }) => {
		findActiveProfile(ctx)?.tabs.goForward();
	}),

	reload: procedure.mutation(({ ctx }) => {
		findActiveProfile(ctx)?.tabs.reload();
	}),

	hideAll: procedure.mutation(({ ctx }) => {
		ctx.pane.hideAllTabs();
	}),

	showActive: procedure.mutation(({ ctx }) => {
		findActiveProfile(ctx)?.tabs.showActive();
	}),
});
```

- [ ] **Step 2: Create profiles router**

```ts
// apps/desktop/src/main/trpc/routers/profiles.ts
import { z } from "zod/v4";

import { procedure, router } from "../trpc";

export const profilesRouter = router({
	activate: procedure
		.input(z.object({ profileId: z.string() }))
		.mutation(({ input, ctx }) => {
			ctx.pane.getOrCreateProfile(input.profileId).extensions.ensureLoaded();
		}),
});
```

- [ ] **Step 3: Create settings router**

```ts
// apps/desktop/src/main/trpc/routers/settings.ts
import { settingsStore } from "../../../stores/settings-store";
import { detectBrowserPath } from "../../detect-browser";
import { procedure, router } from "../trpc";

export const settingsRouter = router({
	detectBrowser: procedure.mutation(() => {
		const detected = detectBrowserPath();
		if (detected) {
			settingsStore.getState().save({ chromiumPath: detected });
		}
		return detected ?? null;
	}),
});
```

- [ ] **Step 4: Create extensions router**

```ts
// apps/desktop/src/main/trpc/routers/extensions.ts
import { z } from "zod/v4";

import { procedure, router } from "../trpc";

export const extensionsRouter = router({
	list: procedure
		.input(z.object({ profileId: z.string() }))
		.query(({ input, ctx }) => {
			const loaded =
				ctx.pane.getProfile(input.profileId)?.extensions.getLoaded() ?? [];
			return loaded.map((ext) => ({
				id: ext.id,
				name: ext.name,
				version: ext.manifest.version,
			}));
		}),
});
```

- [ ] **Step 5: Create cws router**

```ts
// apps/desktop/src/main/trpc/routers/cws.ts
import { z } from "zod/v4";

import { procedure, router } from "../trpc";

export const cwsRouter = router({
	install: procedure
		.input(z.object({ extensionId: z.string() }))
		.mutation(({ input, ctx }) => ctx.pane.extensions.install(input.extensionId)),

	uninstall: procedure
		.input(z.object({ extensionId: z.string() }))
		.mutation(({ input, ctx }) => ctx.pane.extensions.uninstall(input.extensionId)),

	installed: procedure.query(({ ctx }) => ctx.pane.extensions.getInstalled()),
});
```

- [ ] **Step 6: Verify typecheck**

```bash
cd /Users/andrevictor/www/pane && npx turbo run typecheck --filter=@pane/desktop
```

- [ ] **Step 7: Commit**

```bash
git add apps/desktop/src/main/trpc/routers/ && git commit -m "feat(trpc): add domain routers (tabs, profiles, settings, extensions, cws)"
```

---

### Task 6: Create root router

**Files:**
- Create: `apps/desktop/src/main/trpc/router.ts`

- [ ] **Step 1: Create root router composition**

```ts
// apps/desktop/src/main/trpc/router.ts
import { router } from "./trpc";
import { cwsRouter } from "./routers/cws";
import { extensionsRouter } from "./routers/extensions";
import { profilesRouter } from "./routers/profiles";
import { settingsRouter } from "./routers/settings";
import { storesRouter } from "./routers/stores";
import { tabsRouter } from "./routers/tabs";

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

- [ ] **Step 2: Verify typecheck**

```bash
cd /Users/andrevictor/www/pane && npx turbo run typecheck --filter=@pane/desktop
```

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/src/main/trpc/router.ts && git commit -m "feat(trpc): add root router composition"
```

---

### Task 7: Create renderer tRPC client

**Files:**
- Create: `apps/desktop/src/renderer/trpc.ts`

- [ ] **Step 1: Create the typed client singleton**

```ts
// apps/desktop/src/renderer/trpc.ts
import { createTRPCClient } from "@trpc/client";
import { ipcLink } from "trpc-electron/renderer";

import type { AppRouter } from "../main/trpc/router";

export const trpc = createTRPCClient<AppRouter>({
	links: [ipcLink()],
});
```

- [ ] **Step 2: Verify typecheck**

```bash
cd /Users/andrevictor/www/pane && npx turbo run typecheck --filter=@pane/desktop
```

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/src/renderer/trpc.ts && git commit -m "feat(trpc): add renderer tRPC client"
```

---

### Task 8: Update preload

**Files:**
- Modify: `apps/desktop/src/preload/index.ts`

- [ ] **Step 1: Replace preload with exposeElectronTRPC**

Replace the entire file content with:

```ts
// apps/desktop/src/preload/index.ts
import { injectBrowserAction } from "@pane/electron-chrome-extensions/browser-action";
import { exposeElectronTRPC } from "trpc-electron/main";

injectBrowserAction();
exposeElectronTRPC();
```

This removes all `contextBridge.exposeInMainWorld("pane", ...)` and `contextBridge.exposeInMainWorld("electronSync", ...)` code. The `ElectronSync` and `PaneAPI` type exports are also removed.

- [ ] **Step 2: Verify typecheck**

```bash
cd /Users/andrevictor/www/pane && npx turbo run typecheck --filter=@pane/desktop
```

Expected: type errors in files that still import `ElectronSync`/`PaneAPI` or use `window.pane`/`window.electronSync`. These will be fixed in subsequent tasks.

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/src/preload/index.ts && git commit -m "feat(trpc): replace preload with exposeElectronTRPC"
```

---

### Task 9: Rewrite sync middleware

**Files:**
- Modify: `apps/desktop/src/stores/middlewares/sync.ts`

- [ ] **Step 1: Rewrite sync middleware to use tRPC**

Replace the entire file content with:

```ts
// apps/desktop/src/stores/middlewares/sync.ts
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
		let trpc: Awaited<typeof import("../../renderer/trpc")>["trpc"] | null =
			null;

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

- [ ] **Step 2: Verify typecheck**

```bash
cd /Users/andrevictor/www/pane && npx turbo run typecheck --filter=@pane/desktop
```

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/src/stores/middlewares/sync.ts && git commit -m "feat(trpc): rewrite sync middleware to use tRPC subscriptions"
```

---

### Task 10: Wire up main process

**Files:**
- Modify: `apps/desktop/src/main/index.ts`

- [ ] **Step 1: Replace IpcRouter and StoreSync with tRPC handler**

In `apps/desktop/src/main/index.ts`:

1. Remove imports of `IpcRouter` and `StoreSync`
2. Add imports for `createIPCHandler` and `appRouter`
3. Remove `const storeSync = new StoreSync({...})` block
4. In `setup()` function, remove `storeSync.connect(...)`, `const ipcRouter = new IpcRouter(...)`, `ipcRouter.register()`, and `ipcRouter.cleanup()` in the close handler
5. Add `createIPCHandler` call after creating `pane`

The `setup()` function should become:

```ts
function setup() {
	win = createAppWindow();
	createMenu(win.uiView);

	pane = new Pane(win.mainWindow);

	const cleanupIPC = createIPCHandler({
		router: appRouter,
		webContents: win.uiView.webContents,
		createContext: () => ({
			pane: pane!,
			stores: {
				"profile-store": profileStore,
				"tab-store": tabStore,
				"navigation-store": navigationStore,
				"settings-store": settingsStore,
				"extension-store": extensionStore,
			},
		}),
	});

	win.mainWindow.on("resized", () => {
		const [w, h] = win?.mainWindow.getContentSize() ?? [0, 0];
		win?.uiView.setBounds({ x: 0, y: 0, width: w, height: h });
		pane?.resizeAllTabs();
	});

	win.mainWindow.on("closed", () => {
		if (pane) {
			for (const profile of pane.profiles.values()) {
				profile.tabs.destroyAll();
			}
		}
		cleanupIPC();
		win = null;
		pane = null;
	});

	pane.restore();
}
```

Also remove `storeSync.register()` from the `app.whenReady()` block.

Update imports at the top of the file: remove `StoreSync` and `IpcRouter`, add:

```ts
import { createIPCHandler } from "./trpc/ipc";
import { appRouter } from "./trpc/router";
```

- [ ] **Step 2: Verify typecheck**

```bash
cd /Users/andrevictor/www/pane && npx turbo run typecheck --filter=@pane/desktop
```

- [ ] **Step 3: Commit**

```bash
git add apps/desktop/src/main/index.ts && git commit -m "feat(trpc): wire tRPC handler in main process"
```

---

### Task 11: Migrate renderer components

**Files:**
- Modify: `apps/desktop/src/renderer/components/sidebar/sidebar-connected.tsx`
- Modify: `apps/desktop/src/renderer/pages/browser/_components/address-bar-connected.tsx`
- Modify: `apps/desktop/src/renderer/pages/settings/index.tsx`

- [ ] **Step 1: Migrate sidebar-connected.tsx**

In `apps/desktop/src/renderer/components/sidebar/sidebar-connected.tsx`:

Add import at top:
```ts
import { trpc } from "../../trpc";
```

Replace the `ProfileHeader` `onClick` handler (the one with `window.pane?.profiles.activate`):
```ts
onClick={() => {
	profileStore.getState().toggleExpanded(profile.id);
	if (!profile.isExpanded) {
		trpc.profiles.activate.mutate({ profileId: profile.id });
	}
}}
```

Replace `TabItem` `onClick`:
```ts
onClick={() => {
	navigationStore.getState().navigate(Page.BROWSER);
	trpc.tabs.switch.mutate({ tabId: tab.id });
}}
```

Replace the `X` close button `onClick`:
```ts
onClick={(e) => {
	e.stopPropagation();
	trpc.tabs.close.mutate({ tabId: tab.id });
}}
```

Replace `TabNew` `onClick`:
```ts
onClick={() => {
	navigationStore.getState().navigate(Page.BROWSER);
	trpc.tabs.open.mutate({ profileId: profile.id });
}}
```

- [ ] **Step 2: Migrate address-bar-connected.tsx**

In `apps/desktop/src/renderer/pages/browser/_components/address-bar-connected.tsx`:

Add import at top:
```ts
import { trpc } from "../../../trpc";
```

Replace `handleSubmit`:
```ts
const handleSubmit = (e: React.FormEvent) => {
	e.preventDefault();
	if (inputValue.trim()) {
		trpc.tabs.navigate.mutate({ url: inputValue.trim() });
		(document.activeElement as HTMLElement)?.blur();
	}
};
```

Replace `AddressBarNav` props:
```ts
<AddressBarNav
	onBack={() => trpc.tabs.goBack.mutate()}
	onForward={() => trpc.tabs.goForward.mutate()}
	onReload={() => trpc.tabs.reload.mutate()}
/>
```

- [ ] **Step 3: Migrate settings/index.tsx**

In `apps/desktop/src/renderer/pages/settings/index.tsx`:

Add import at top:
```ts
import { trpc } from "../../trpc";
```

Replace `useEffect` for loading extensions:
```ts
useEffect(() => {
	trpc.cws.installed.query().then(setExtensions);
}, []);
```

Replace `handleInstall`:
```ts
const handleInstall = useCallback(async (value: string) => {
	const id = parseExtensionId(value);
	if (!id) return;

	setIsInstalling(true);
	try {
		const ext = await trpc.cws.install.mutate({ extensionId: id });
		if (ext) {
			const updated = await trpc.cws.installed.query();
			setExtensions(updated);
		} else {
			toast.error("Failed to install extension");
		}
	} catch {
		toast.error("Failed to install extension");
	} finally {
		setIsInstalling(false);
	}
}, []);
```

Replace `handleUninstall`:
```ts
const handleUninstall = useCallback(async () => {
	if (!uninstallTarget) return;

	try {
		await trpc.cws.uninstall.mutate({ extensionId: uninstallTarget.id });
		setExtensions((prev) => prev.filter((e) => e.id !== uninstallTarget.id));
		setUninstallTarget(null);
	} catch {
		toast.error("Failed to uninstall extension");
	}
}, [uninstallTarget]);
```

Replace the auto-detect button:
```ts
onClick={() => trpc.settings.detectBrowser.mutate()}
```

- [ ] **Step 4: Verify typecheck**

```bash
cd /Users/andrevictor/www/pane && npx turbo run typecheck --filter=@pane/desktop
```

- [ ] **Step 5: Commit**

```bash
git add apps/desktop/src/renderer/components/sidebar/sidebar-connected.tsx apps/desktop/src/renderer/pages/browser/_components/address-bar-connected.tsx apps/desktop/src/renderer/pages/settings/index.tsx && git commit -m "feat(trpc): migrate renderer components to tRPC client"
```

---

### Task 12: Clean up old files and type augmentation

**Files:**
- Delete: `apps/desktop/src/main/ipc.ts`
- Delete: `apps/desktop/src/stores/middlewares/store-sync.ts`
- Modify: `apps/desktop/src/renderer/main.tsx`

- [ ] **Step 1: Delete old IPC router**

```bash
rm /Users/andrevictor/www/pane/apps/desktop/src/main/ipc.ts
```

- [ ] **Step 2: Delete old StoreSync**

```bash
rm /Users/andrevictor/www/pane/apps/desktop/src/stores/middlewares/store-sync.ts
```

- [ ] **Step 3: Remove Window type augmentation and old imports from main.tsx**

Replace `apps/desktop/src/renderer/main.tsx` with:

```tsx
// apps/desktop/src/renderer/main.tsx
import { StrictMode } from "react";
import { createRoot } from "react-dom/client";

import { App } from "./components/app";
import "./styles/globals.css";

const root = document.getElementById("root");

if (!root) {
	throw new Error("Root element not found");
}

createRoot(root).render(
	<StrictMode>
		<App />
	</StrictMode>,
);
```

- [ ] **Step 4: Verify no remaining references to old code**

```bash
cd /Users/andrevictor/www/pane && grep -rn "window\.pane\|window\.electronSync\|StoreSync\|IpcRouter\|ElectronSync\|PaneAPI" apps/desktop/src/ --include="*.ts" --include="*.tsx" | grep -v node_modules | grep -v ".d.ts"
```

Expected: no output (all references removed)

- [ ] **Step 5: Verify typecheck**

```bash
cd /Users/andrevictor/www/pane && npx turbo run typecheck --filter=@pane/desktop
```

- [ ] **Step 6: Commit**

```bash
git add -A apps/desktop/src/main/ipc.ts apps/desktop/src/stores/middlewares/store-sync.ts apps/desktop/src/renderer/main.tsx && git commit -m "chore: remove old IPC router, StoreSync, and Window type augmentation"
```

---

### Task 13: Build and smoke test

**Files:** None (verification only)

- [ ] **Step 1: Full build**

```bash
cd /Users/andrevictor/www/pane && npx turbo run build --filter=@pane/desktop
```

Expected: success with no errors

- [ ] **Step 2: Run the app**

```bash
cd /Users/andrevictor/www/pane && npx turbo run dev --filter=@pane/desktop
```

- [ ] **Step 3: Smoke test checklist**

Test each of these in the running app:

1. **Sidebar**: Click a profile — it should expand showing tabs + "New tab" button
2. **New tab**: Click "New tab" — a browser tab should open
3. **Tab switching**: Click between tabs — they should switch
4. **Tab close**: Click X on a tab — it should close
5. **Address bar**: Type a URL and press Enter — it should navigate
6. **Back/Forward/Reload**: Navigation buttons should work
7. **Settings**: Open settings page, click "Auto-detect" — browser path should populate
8. **Extensions**: Install/uninstall flow should work
9. **Profile delete**: Delete a profile — it should be removed
10. **Store sync**: Changes in main process (tab title updates, favicon updates) should appear in the renderer sidebar

- [ ] **Step 4: Commit any fixes needed**

If any issues found during smoke testing, fix them and commit.
