/**
 * @file windows.ts
 *
 * Implements a subset of the `chrome.windows` API for extension service workers
 * running inside Electron. Electron does not expose `chrome.windows` natively
 * in SW contexts, so this shim handles IPC calls forwarded from the SW preload
 * and delegates to the application via `ctx.store`.
 *
 * The implementation covers the window methods most commonly called by
 * extensions (`create`, `get`, `getCurrent`, `getLastFocused`, `getAll`,
 * `update`, `remove`). Window objects returned by this shim are synthetic —
 * they reflect the last focused Electron `BrowserWindow` rather than a
 * fully-modelled Chrome window, which is sufficient for extensions that only
 * need basic window information or want to trigger tab/window creation.
 *
 * When an extension calls `chrome.windows.create`, the shim calls
 * `ctx.store.createTab()` which invokes the consumer-supplied callback. This
 * keeps the shim decoupled from any particular browser chrome implementation,
 * since Electron doesn't have a native concept of extension-managed windows.
 */

import { type Session } from "electron";
import type { ExtensionContext } from "../context";

/**
 * Resolves a potentially relative extension URL to a fully qualified
 * `chrome-extension://` URL.
 *
 * Extensions sometimes pass relative paths (e.g. `"options.html"`) to
 * `chrome.windows.create`. This helper expands them to absolute URLs using the
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
 * Constructs a minimal object that matches the shape of a `chrome.Window`.
 *
 * The shim reads the last focused `BrowserWindow` from `ctx.store` to produce
 * plausible `id` and `focused` values. Positional and dimensional fields
 * (`top`, `left`, `width`, `height`) are placeholder values because Electron
 * windows are managed by the host application, not by the extension layer.
 *
 * @param ctx - The extension context, providing access to `ctx.store`.
 * @returns    A plain object conforming to the `chrome.Window` shape.
 */
function makeWindowObject(ctx: ExtensionContext): Record<string, unknown> {
	const win = ctx.store.getLastFocusedWindow();

	return {
		id: win?.id ?? 1,
		focused: win ? win.isFocused() : true,
		// Positional values are placeholders — we don't track window geometry here.
		top: 0,
		left: 0,
		width: 1280,
		height: 800,
		type: "normal",
		state: "normal",
	};
}

/**
 * Handles all `chrome.windows.*` method calls forwarded from the SW preload.
 *
 * Supported methods:
 * - `create`         — open a new tab via `ctx.store.createTab()` and return a synthetic window object
 * - `get`            — return a synthetic window object (no live lookup by ID)
 * - `getCurrent`     — return a synthetic window object for the current context
 * - `getLastFocused` — return a synthetic window object for the last focused window
 * - `getAll`         — return a single-element array containing the synthetic window object
 * - `update`         — acknowledged but not acted upon (returns a stub window object)
 * - `remove`         — acknowledged but not acted upon (returns `undefined`)
 *
 * @param ctx    - The extension context, providing session and store access.
 * @param method - The `chrome.windows` method name (e.g. `"create"`).
 * @param args   - Method arguments forwarded verbatim from the SW preload.
 * @returns      The return value to send back to the extension, or `undefined`.
 */
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

			// Delegate tab/window creation to the host application. The promise is
			// intentionally not awaited — the extension receives a synthetic window
			// object immediately, matching Chrome's non-blocking behaviour.
			ctx.store.createTab({ url }).catch(() => {});

			return makeWindowObject(ctx);
		}
		case "get":
		case "getCurrent":
		case "getLastFocused":
			return makeWindowObject(ctx);
		case "getAll":
			// Return a single window representing the current state. Extensions that
			// enumerate all windows typically just need at least one entry to proceed.
			return [makeWindowObject(ctx)];
		case "update":
			// Acknowledged but not implemented; return a stub to avoid extension errors.
			return makeWindowObject(ctx);
		case "remove":
			return undefined;
		default:
			return undefined;
	}
}
