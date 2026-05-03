/**
 * @file tabs.ts
 *
 * Implements a subset of the `chrome.tabs` API for extension service workers
 * running inside Electron. Electron does not expose `chrome.tabs` natively in
 * SW contexts, so this shim handles IPC calls forwarded from the SW preload
 * and delegates to the application via `ctx.store`.
 *
 * The implementation is intentionally minimal — it covers the methods most
 * commonly called by extensions (create, get, query, update, remove) and
 * returns a synthetic tab object whose shape satisfies the Chrome Tabs API
 * without requiring a full tab model inside the shim layer.
 *
 * Tab identity is managed by the host application through `ctx.store`. When an
 * extension calls `chrome.tabs.create`, the shim calls `ctx.store.createTab()`
 * which invokes the consumer-supplied callback, keeping tab management generic
 * and decoupled from any particular browser chrome implementation.
 */

import { type Session } from "electron";
import type { ExtensionContext } from "../context";

/**
 * Resolves a potentially relative extension URL to a fully qualified
 * `chrome-extension://` URL.
 *
 * Extensions sometimes pass relative paths (e.g. `"popup.html"`) to
 * `chrome.tabs.create`. This helper expands them to absolute URLs using the
 * first registered extension in the session. Absolute `chrome-extension://`
 * and `http(s)://` URLs are returned unchanged.
 *
 * @param ses - The Electron session used to look up the extension origin.
 * @param url - The URL to resolve (may be relative, absolute, or external).
 * @returns    The resolved absolute URL.
 */
function resolveExtensionUrl(ses: Session, url: string): string {
	if (url.startsWith("chrome-extension://") || url.startsWith("http")) {
		return url;
	}

	const ext = ses.extensions.getAllExtensions()[0];

	if (!ext) {
		return url;
	}

	return `chrome-extension://${ext.id}/${url.replace(/^\//, "")}`;
}

/**
 * Constructs a minimal object that matches the shape of a `chrome.Tab`.
 *
 * The shim does not maintain a live tab model, so fields like `index`,
 * `windowId`, and `title` are filled with placeholder values. Extensions that
 * depend on accurate tab metadata may not behave correctly, but this covers
 * the common case of extensions simply opening or querying for a tab.
 *
 * @param tabId  - The tab ID to embed in the returned object.
 * @param url    - The URL the tab is currently showing.
 * @param active - Whether the tab is the active tab in its window.
 * @returns      A plain object conforming to the `chrome.Tab` shape.
 */
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

/**
 * Handles all `chrome.tabs.*` method calls forwarded from the SW preload.
 *
 * Supported methods:
 * - `create`  — open a new tab via `ctx.store.createTab()` and return a synthetic tab object
 * - `get`     — return a minimal tab object for the given tab ID (no live lookup)
 * - `query`   — return the currently active tab from `ctx.store`, or an empty array
 * - `update`  — acknowledged but not acted upon (returns a stub tab object)
 * - `remove`  — acknowledged but not acted upon (returns `undefined`)
 *
 * @param ctx    - The extension context, providing session and store access.
 * @param method - The `chrome.tabs` method name (e.g. `"create"`).
 * @param args   - Method arguments forwarded verbatim from the SW preload.
 * @returns      The return value to send back to the extension, or `undefined`.
 */
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

			// Delegate tab creation to the host application. The result is intentionally
			// not awaited — the extension receives a synthetic tab object immediately,
			// matching Chrome's non-blocking behaviour.
			ctx.store.createTab({ url }).catch(() => {});

			// Tab ID 0 is a placeholder; a real implementation would return the actual
			// new tab's ID once the store resolves.
			return makeTabObject(0, url, true);
		}
		case "get": {
			const [tabId] = args as [number];

			// No live tab model — return a stub so callers don't receive null/undefined.
			return makeTabObject(tabId, "", false);
		}
		case "query": {
			const activeTab = ctx.store.getActiveTabOfCurrentWindow();

			if (!activeTab) {
				return [];
			}

			return [makeTabObject(activeTab.id, activeTab.getURL(), true)];
		}
		case "update":
			// Acknowledged but not implemented; return a stub to avoid extension errors.
			return makeTabObject(0, "", false);
		case "remove":
			return undefined;
		default:
			return undefined;
	}
}
