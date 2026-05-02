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
      return [makeTabObject(activeTab.id, activeTab.getURL(), true)];
    }
    case "update":
      return makeTabObject(0, "", false);
    case "remove":
      return undefined;
    default:
      return undefined;
  }
}
