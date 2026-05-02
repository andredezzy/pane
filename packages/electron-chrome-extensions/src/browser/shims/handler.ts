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
