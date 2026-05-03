/**
 * @file frame.js — Frame Preload
 *
 * Registered as `type: "frame"` on each extension session via
 * `session.registerPreloadScript()` in the ECE constructor. This script
 * runs in ALL extension pages: popups, options pages, `app.html`, etc.
 *
 * ## Purpose
 *
 * Patches missing or broken Chrome APIs in extension page (frame) contexts
 * where `chrome` is a regular object (NOT the V8 Proxy found in SW contexts).
 * Since `chrome` is a regular object here, we can freely add/modify
 * properties without the invariant violations that plague SW contexts.
 *
 * Provides four categories of patches:
 *
 * 1. **IPC Bridge (`__crxIpc`)** — Same pattern as sw.js: projects an
 *    `ipcRenderer.invoke` wrapper into the main world so frame-context
 *    APIs can reach the ECE main-process handlers.
 *
 * 2. **Chrome API stubs** — Fills in `chrome.extension` methods that
 *    Electron doesn't provide (`getViews`, `isAllowedIncognitoAccess`,
 *    `isAllowedFileSchemeAccess`).
 *
 * 3. **IPC-backed overrides** — Replaces `chrome.windows.create` and
 *    patches `chrome.tabs.create` with IPC-backed implementations,
 *    because the native bindings either throw or no-op in an extension
 *    renderer context.
 *
 * 4. **Blank popup fallback** — Detects extensions that render a blank
 *    popup (e.g. NordPass when unauthenticated) and opens the extension's
 *    full-page UI (`app.html`) as a tab instead.
 *
 * ## Context Isolation
 *
 * Like sw.js, this preload runs in the isolated world. The extension
 * page's own scripts run in the main world where `chrome` lives. We use:
 *
 *   - `contextBridge.exposeInMainWorld(key, obj)` — project `__crxIpc`
 *   - `contextBridge.executeInMainWorld({ func })` — patch chrome in-place
 *
 * Functions passed to `executeInMainWorld` are serialised and CANNOT close
 * over variables from this file.
 *
 * ## Design Constraints
 *
 * This preload intentionally avoids replacing ENTIRE chrome namespaces.
 * Overwriting a namespace object (e.g. `chrome.tabs = {...}`) would sever
 * Electron's internal bindings which hold direct references to the original
 * objects. We only add or replace INDIVIDUAL methods that are missing or
 * broken, preserving all native methods on the namespace.
 */

const { contextBridge, ipcRenderer } = require("electron");

// =========================================================================
// §1 — IPC Bridge
// =========================================================================
//
// Same pattern as sw.js §1. Extension page scripts call
// `__crxIpc.invoke(namespace, method, ...args)` to reach ECE's main-process
// handlers via the `crx-shim` IPC channel.
//
// No `!process.contextIsolated` fallback here (unlike sw.js) because
// extension pages in Pane always run with context isolation enabled.

if (process.contextIsolated) {
	contextBridge.exposeInMainWorld("__crxIpc", {
		invoke(namespace, method, ...args) {
			return ipcRenderer.invoke("crx-shim", namespace, method, ...args);
		},
	});
}

if ("executeInMainWorld" in contextBridge) {
	// =======================================================================
	// §2 — Main-World Patches
	// =======================================================================
	//
	// All code below runs INSIDE the extension page's main world so it can
	// access and modify `chrome`. The function is self-contained; it relies
	// only on `globalThis` values set up by Electron or by §1 above.

	contextBridge.executeInMainWorld({
		func: () => {
			// -------------------------------------------------------------------
			// Process polyfill
			// -------------------------------------------------------------------
			// Same rationale as sw.js: extension bundles frequently reference
			// `process.env.NODE_ENV` at runtime. Node's `process` global is
			// not available in a browser renderer context.

			if (typeof globalThis.process === "undefined") {
				globalThis.process = {
					env: { NODE_ENV: "production" },
					platform: "darwin",
					version: "",
				};
			}

			var chrome = globalThis.chrome;

			if (!chrome) {
				return;
			}

			// -------------------------------------------------------------------
			// chrome.extension stubs
			// -------------------------------------------------------------------
			// Several `chrome.extension` methods are absent or throw in
			// Electron's chrome binding because they require browser internals
			// (cross-renderer view registry, profile incognito state). We patch
			// only the missing ones so existing functionality is not displaced.

			try {
				// getViews() normally returns live Window objects for other
				// extension pages. Electron has no cross-renderer registry, so
				// we return an empty array rather than undefined/throw.
				if (
					chrome.extension &&
					typeof chrome.extension.getViews !== "function"
				) {
					chrome.extension.getViews = () => [];
				}
			} catch (e) {}

			try {
				// isAllowedIncognitoAccess() checks the extension's permission.
				// We always return false — Pane does not support incognito profiles.
				if (
					chrome.extension &&
					typeof chrome.extension.isAllowedIncognitoAccess !== "function"
				) {
					chrome.extension.isAllowedIncognitoAccess = (cb) => {
						if (cb) {
							cb(false);
						}

						return Promise.resolve(false);
					};
				}
			} catch (e) {}

			try {
				// isAllowedFileSchemeAccess() checks whether the extension may
				// access file:// URLs. Return false as a safe default.
				if (
					chrome.extension &&
					typeof chrome.extension.isAllowedFileSchemeAccess !== "function"
				) {
					chrome.extension.isAllowedFileSchemeAccess = (cb) => {
						if (cb) {
							cb(false);
						}

						return Promise.resolve(false);
					};
				}
			} catch (e) {}

			// -------------------------------------------------------------------
			// Block window.close()
			// -------------------------------------------------------------------
			// Extension popups call `window.close()` to dismiss themselves. In a
			// real browser this closes the popup window. In Electron the popup is
			// a BrowserView/WebContentsView managed by the host app — if the
			// extension calls `window.close()`, the underlying Electron window
			// would be destroyed, leaving the BrowserView in a broken state.
			//
			// We replace `window.close` with a no-op. The host app handles
			// hiding the popup at the right time via its own UI logic.

			globalThis.close = () => {};

			if (globalThis.window) {
				globalThis.window.close = () => {};
			}

			// -------------------------------------------------------------------
			// IPC-backed chrome.windows.create / chrome.tabs.create
			// -------------------------------------------------------------------
			// These APIs open new windows/tabs. In Electron, the renderer has no
			// authority to spawn BrowserWindows directly — that must go through
			// the main process via IPC.
			//
			// windows.create: ALWAYS replaced because the native binding either
			//   does nothing or throws in an extension renderer context.
			//
			// tabs.create: only patched when the native binding is MISSING. We
			//   do NOT replace an existing tabs.create because doing so would
			//   sever the native chrome.tabs binding. tabs is intentionally NOT
			//   fully shimmed — a wholesale replacement would break native
			//   methods like tabs.sendMessage that extensions depend on for
			//   messaging between their own components.

			var ipc = globalThis.__crxIpc;

			if (!ipc) {
				return;
			}

			try {
				if (chrome.windows) {
					chrome.windows.create = (opts, cb) => {
						var p = ipc.invoke("windows", "create", opts);

						if (cb) {
							p.then(cb);
						}

						return p;
					};
				}
			} catch (e) {}

			try {
				if (chrome.tabs && typeof chrome.tabs.create !== "function") {
					chrome.tabs.create = (opts, cb) => {
						var p = ipc.invoke("tabs", "create", opts);

						if (cb) {
							p.then(cb);
						}

						return p;
					};
				}
			} catch (e) {}
		},
	});

	// =======================================================================
	// §3 — Blank Popup Fallback
	// =======================================================================
	//
	// Some extensions (e.g. NordPass when unauthenticated) render a completely
	// blank popup because their SW hasn't set a popup URL via
	// `chrome.action.setPopup()`. The blank popup is an intentional state —
	// the extension delegates login to a separate full-page view (`app.html`).
	//
	// After a 1.5-second delay, we check if the popup is blank and, if so,
	// open the extension's full-page view as a tab via IPC.
	//
	// Guards:
	//   - Skip if viewport is large (not a popup, probably a full-page tab)
	//   - Skip if the manifest declares `default_popup` (the extension
	//     intentionally shows a popup UI, even if it's still loading)
	//   - Blank = fewer than 3 DOM elements in `#app` or `<body>`

	setTimeout(() => {
		const shouldFallback = contextBridge.executeInMainWorld({
			func: () => {
				if (window.innerWidth > 500 || window.innerHeight > 600) {
					return false;
				}

				try {
					var m = globalThis.chrome?.runtime?.getManifest?.();

					if (m) {
						var popup =
							m.action?.default_popup || m.browser_action?.default_popup;

						if (popup) {
							return false;
						}
					}
				} catch (e) {}

				var app = document.getElementById("app") || document.body;

				return app ? app.querySelectorAll("*").length <= 3 : false;
			},
		});

		if (shouldFallback) {
			const extUrl = contextBridge.executeInMainWorld({
				func: () => {
					try {
						return globalThis.chrome?.runtime?.getURL?.("app.html");
					} catch (e) {
						return null;
					}
				},
			});

			if (extUrl) {
				ipcRenderer.invoke("crx-shim", "windows", "create", { url: extUrl });
			}
		}
	}, 1500);
}
