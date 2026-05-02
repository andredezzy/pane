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
