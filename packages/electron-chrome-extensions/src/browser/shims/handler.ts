/**
 * @file handler.ts
 *
 * Entry point for the Chrome API shim layer. Registers IPC handlers that
 * receive `chrome.*` method calls forwarded from the SW and frame preload
 * scripts (`sw.js`, `frame.js`) and routes them to the appropriate shim
 * implementation.
 *
 * ## IPC Channel
 *
 * All communication uses the `crx-shim` IPC channel. The preload scripts
 * call `ipcRenderer.invoke("crx-shim", namespace, method, ...args)` and
 * this file dispatches those calls to the correct handler module.
 *
 * ## Why Two Registration Paths?
 *
 * Electron 41 introduced per-worker IPC channels for service workers — a
 * SW does NOT go through the global `ipcMain` channel. However, non-SW
 * extension pages (popup pages, options pages, etc.) still use the
 * standard `ipcMain.handle` path.
 *
 * This file therefore maintains **two** registration points:
 *
 * 1. **Global** (`ipcMain.handle("crx-shim")`) — covers frame contexts
 *    (popup pages, options pages, sidepanels, etc.) that communicate
 *    over the standard IPC channel. The extension ID is unknown in this
 *    path because frame contexts don't carry SW scope information.
 *
 * 2. **Per-SW** (`serviceWorker.ipc.handle("crx-shim")`) — registered
 *    for each new extension SW instance on `running-status-changed`.
 *    Each SW has its own IPC object that must be registered independently
 *    and exactly once per worker lifetime. The extension ID is extracted
 *    from the SW's `scope` URL (`chrome-extension://<id>/`) so that
 *    namespace handlers can attribute calls to the correct extension.
 *
 * Both paths converge in the shared {@link dispatch} function.
 *
 * ## Extension ID Extraction
 *
 * When a SW calls `chrome.action.setPopup({popup: "index.html"})`, the
 * shim must know WHICH extension is calling so it can register the popup
 * URL with the correct `BrowserActionAPI` entry. Without the extension ID,
 * `getAllExtensions()[0]` would be used, which returns the wrong extension
 * when multiple extensions are loaded in the same session.
 *
 * The per-SW handler extracts the extension ID from `sw.scope` (which is
 * always `chrome-extension://<extension-id>/`) at registration time and
 * captures it in a closure so it's available for every subsequent call
 * from that specific worker.
 */

import { ipcMain, type Session } from "electron";
import type { ExtensionContext } from "../context";
import { handleAlarms } from "./alarms";
import { handleIdle } from "./idle";
import { handleTabs } from "./tabs";
import { handleWindows } from "./windows";

/**
 * Guards against registering the global `ipcMain.handle("crx-shim")`
 * handler more than once. Because `registerShimHandler` may be called for
 * each new `ExtensionContext` (one per profile session), the guard ensures
 * only the first call installs the handler.
 */
let globalRegistered = false;

/**
 * Registers the global IPC handler for frame contexts (popup pages, options
 * pages, sidepanels). Only installed once per process lifetime.
 *
 * Frame contexts don't have a SW scope, so the extension ID is passed as
 * `undefined` to {@link dispatch}. Namespace handlers that need the
 * extension ID (e.g. `action.setPopup`) fall back to
 * `getAllExtensions()[0]`, which works in the frame path because the popup
 * is always associated with the extension that opened it.
 *
 * @param ctx - The active extension context, passed through to dispatch.
 */
export function registerShimHandler(ctx: ExtensionContext) {
	if (globalRegistered) {
		return;
	}

	globalRegistered = true;

	ipcMain.handle(
		"crx-shim",
		(_event, namespace: string, method: string, ...args: unknown[]) => {
			return dispatch(ctx, undefined, namespace, method, ...args);
		},
	);
}

/**
 * Tracks which `Session` instances already have their per-SW listener
 * registered. A `WeakSet` is used so that garbage-collected sessions are
 * automatically removed without an explicit cleanup step.
 */
const registeredSessions = new WeakSet<Session>();

/**
 * Registers a `running-status-changed` listener on the session's service
 * workers that installs a `crx-shim` IPC handler on each new extension SW
 * instance. Safe to call multiple times — subsequent calls for the same
 * session are no-ops.
 *
 * ### Why listen to `running-status-changed` instead of registering once?
 *
 * In Electron 41+, each SW instance has its own IPC channel object that is
 * only valid for the lifetime of that worker. There is no way to
 * pre-register a handler that covers all future instances, so we hook into
 * the lifecycle event and register the handler the moment a new worker
 * starts.
 *
 * ### Extension ID extraction
 *
 * Each extension SW has a `scope` property of the form
 * `chrome-extension://<extension-id>/`. We extract the extension ID from
 * this URL at registration time and capture it in the handler closure.
 * This allows {@link dispatch} to attribute calls (especially
 * `action.setPopup`) to the correct extension without relying on
 * `getAllExtensions()[0]`.
 *
 * @param ctx - The active extension context, providing session access.
 */
export function registerShimHandlerForSession(ctx: ExtensionContext) {
	const ses = ctx.session;

	if (registeredSessions.has(ses)) {
		return;
	}

	registeredSessions.add(ses);

	// Tracks worker instances that have already received the IPC handler
	// so we don't register it twice if the event fires multiple times for
	// the same versionId.
	const workers = new WeakSet();

	ses.serviceWorkers.on(
		"running-status-changed",
		({
			runningStatus,
			versionId,
		}: Electron.Event<Electron.ServiceWorkersRunningStatusChangedEventParams>) => {
			// Only register when the worker is starting. The handler registered
			// here stays active until the worker stops, so we must not re-register
			// on "running" or "stopped".
			if (runningStatus !== "starting") {
				return;
			}

			// `getWorkerFromVersionID` is not yet part of Electron's public types.
			const sw = (ses as any).serviceWorkers.getWorkerFromVersionID(versionId);

			if (!sw || workers.has(sw)) {
				return;
			}

			// Only shim extension SWs — ignore non-extension workers (e.g. web SWs).
			if (!sw.scope?.startsWith("chrome-extension://")) {
				return;
			}

			workers.add(sw);

			// Extract extension ID from scope: "chrome-extension://<id>/" → "<id>"
			const extensionId = sw.scope.match(/^chrome-extension:\/\/([^/]+)/)?.[1];

			sw.ipc.handle(
				"crx-shim",
				(
					_event: unknown,
					namespace: string,
					method: string,
					...args: unknown[]
				) => {
					return dispatch(ctx, extensionId, namespace, method, ...args);
				},
			);
		},
	);
}

/**
 * Routes an incoming `crx-shim` IPC call to the appropriate namespace
 * handler.
 *
 * Each `namespace` maps to a dedicated shim module:
 *   - `"alarms"` → alarms.ts (setTimeout-based alarm scheduling)
 *   - `"idle"`    → idle.ts (powerMonitor-based idle detection)
 *   - `"windows"` → windows.ts (BrowserWindow management)
 *   - `"tabs"`    → tabs.ts (tab creation/query)
 *   - `"action"`  → inline handler (popup URL registration)
 *   - `"__log"`   → inline handler (SW debug logging)
 *
 * Unknown namespaces return `undefined` so callers receive a resolved
 * (but empty) response instead of a rejected promise.
 *
 * @param ctx         - The active extension context.
 * @param extensionId - The calling extension's ID, extracted from the SW
 *                      scope URL. `undefined` for frame contexts where the
 *                      SW scope is not available.
 * @param namespace   - The Chrome API namespace (e.g. `"alarms"`).
 * @param method      - The method name within the namespace (e.g. `"create"`).
 * @param args        - Forwarded method arguments from the preload.
 * @returns           The value to serialize back to the caller.
 */
function dispatch(
	ctx: ExtensionContext,
	extensionId: string | undefined,
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
			// `chrome.action.setPopup` lets an extension dynamically change
			// the URL of its browser-action popup (e.g. NordPass sets it to
			// "index.html" after authentication). We propagate this via the
			// context event bus so BrowserActionAPI registers the URL.
			//
			// `extensionId` is preferred (accurate, from SW scope). Falls back
			// to `getAllExtensions()[0]` for frame contexts where the SW scope
			// is unavailable.
			if (method === "setPopup") {
				const [details] = args as [{ popup?: string }];

				const extId =
					extensionId ?? ctx.session.extensions.getAllExtensions()[0]?.id;

				if (extId && details?.popup) {
					ctx.emit("set-popup", extId, details.popup);
				}
			}

			return undefined;
		}
		case "__log":
			console.log(`[SW]`, method, ...args);

			return undefined;
		default:
			return undefined;
	}
}
