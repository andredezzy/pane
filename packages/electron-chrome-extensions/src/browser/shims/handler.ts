/**
 * @file handler.ts
 *
 * Entry point for the Chrome API shim layer. Registers IPC handlers that
 * receive `chrome.*` method calls forwarded from the SW preload script and
 * routes them to the appropriate shim implementation.
 *
 * ## Why two registration paths?
 *
 * Electron 41 introduced per-worker IPC channels for service workers — a SW
 * does NOT go through the global `ipcMain` channel. However, non-SW extension
 * pages (popup pages, options pages, etc.) still use the standard
 * `ipcMain.handle` path. This file therefore maintains **two** registration
 * points:
 *
 * 1. **Global** — `ipcMain.handle("crx-shim")` covers frame contexts (popup
 *    pages, options pages, sidepanels, etc.) that communicate over the
 *    standard IPC channel.
 *
 * 2. **Per-SW** — `serviceWorker.ipc.handle("crx-shim")` is registered for
 *    each new extension service worker instance on `running-status-changed`.
 *    Each SW instance gets its own IPC object that must be registered
 *    independently and exactly once per worker lifetime.
 *
 * Both paths converge in the shared {@link dispatch} function, which routes
 * the call to the correct namespace handler.
 */

import { ipcMain, type Session } from "electron";
import type { ExtensionContext } from "../context";
import { handleAlarms } from "./alarms";
import { handleIdle } from "./idle";
import { handleWindows } from "./windows";
import { handleTabs } from "./tabs";

/**
 * Guards against registering the global `ipcMain.handle("crx-shim")` handler
 * more than once. Because `registerShimHandler` may be called for each new
 * `ExtensionContext`, the guard ensures only the first call installs the
 * handler.
 */
let globalRegistered = false;

/**
 * Registers the global IPC handler for frame contexts (popup pages, options
 * pages, sidepanels). Only installed once per process lifetime.
 *
 * @param ctx - The active extension context, passed through to {@link dispatch}.
 */
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
 * only valid for the lifetime of that worker. There is no way to pre-register
 * a handler that covers all future instances, so we hook into the lifecycle
 * event and register the handler the moment a new worker starts.
 *
 * @param ctx - The active extension context, providing session access.
 */
export function registerShimHandlerForSession(ctx: ExtensionContext) {
  const ses = ctx.session;
  if (registeredSessions.has(ses)) return;
  registeredSessions.add(ses);

  // Tracks worker instances that have already received the IPC handler so we
  // don't register it twice if the event fires multiple times for the same
  // versionId.
  const workers = new WeakSet();

  ses.serviceWorkers.on("running-status-changed", ({
    runningStatus,
    versionId,
  }: Electron.Event<Electron.ServiceWorkersRunningStatusChangedEventParams>) => {
    // Only register when the worker is starting. The handler registered here
    // stays active until the worker stops, so we must not re-register on
    // "running" or "stopped".
    if (runningStatus !== "starting") return;

    // `getWorkerFromVersionID` is not yet part of Electron's public types,
    // hence the `as any` cast.
    const sw = (ses as any).serviceWorkers.getWorkerFromVersionID(versionId);
    if (!sw || workers.has(sw)) return;

    // Only shim extension SWs — ignore non-extension workers (e.g. web SWs).
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

/**
 * Routes an incoming `crx-shim` IPC call to the appropriate namespace handler.
 *
 * Each `namespace` maps to a dedicated shim module (`alarms`, `idle`,
 * `windows`, `tabs`, `action`). Unknown namespaces return `undefined` so that
 * callers receive a resolved (but empty) response instead of a rejected
 * promise.
 *
 * @param ctx       - The active extension context.
 * @param namespace - The Chrome API namespace (e.g. `"alarms"`, `"tabs"`).
 * @param method    - The method name within the namespace (e.g. `"create"`).
 * @param args      - Forwarded method arguments from the SW preload.
 * @returns         The value to serialize back to the extension's SW.
 */
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
      // `chrome.action.setPopup` lets an extension dynamically change the URL
      // of its browser-action popup. We propagate this via the context event
      // bus so the host application can update its UI accordingly.
      if (method === "setPopup") {
        const [details] = args as [{ popup?: string }];
        const ext = ctx.session.extensions.getAllExtensions()[0];
        if (ext && details?.popup) {
          ctx.emit("set-popup", ext.id, details.popup);
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
