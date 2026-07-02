import { EventEmitter } from "node:events";
import fs, { existsSync } from "node:fs";
import { createRequire } from "node:module";
import path from "node:path";
import { app, session as electronSession } from "electron";
import { BrowserActionAPI } from "./api/browser-action";
import { CommandsAPI } from "./api/commands";
import { ContextMenusAPI } from "./api/context-menus";
import { CookiesAPI } from "./api/cookies";
import { NotificationsAPI } from "./api/notifications";
import { PermissionsAPI } from "./api/permissions";
import { RuntimeAPI } from "./api/runtime";
import { TabsAPI } from "./api/tabs";
import { WebNavigationAPI } from "./api/web-navigation";
import { WindowsAPI } from "./api/windows";
import { ExtensionContext } from "./context";
import { ChromeExtensionImpl } from "./impl";
import { checkLicense, License } from "./license";
import { readLoadedExtensionManifest } from "./manifest";
import { resolvePartition } from "./partition";
import { ExtensionRouter } from "./router";
import { sanitizeExtensionManifests } from "./sanitize-manifests";
import { destroyAlarms } from "./shims/alarms";
import {
	registerShimHandler,
	registerShimHandlerForSession,
} from "./shims/handler";
import { destroyIdle } from "./shims/idle";
import { ExtensionStore } from "./store";

function checkVersion() {
	const electronVersion = process.versions.electron;

	if (electronVersion && parseInt(electronVersion.split(".")[0], 10) < 35) {
		console.warn("electron-chrome-extensions requires electron@>=35.0.0");
	}
}

function resolvePreloadPath(modulePath?: string) {
	// Attempt to resolve preload path from module exports
	try {
		return createRequire(__dirname).resolve(
			"electron-chrome-extensions/preload",
		);
	} catch (error) {
		if (process.env.NODE_ENV !== "production") {
			console.error(error);
		}
	}

	const preloadFilename = "chrome-extension-api.preload.js";

	// Deprecated: use modulePath if provided
	if (modulePath) {
		process.emitWarning(
			'electron-chrome-extensions: "modulePath" is deprecated and will be removed in future versions.',
			{ type: "DeprecationWarning" },
		);

		return path.join(modulePath, "dist", preloadFilename);
	}

	// Fallback to preload relative to entrypoint directory
	return path.join(__dirname, preloadFilename);
}

export interface ChromeExtensionOptions extends ChromeExtensionImpl {
	/**
	 * License used to distribute electron-chrome-extensions.
	 *
	 * See LICENSE.md for more details.
	 */
	license: License;

	/**
	 * Session to add Chrome extension support in.
	 * Defaults to `session.defaultSession`.
	 */
	session?: Electron.Session;

	/**
	 * Path to electron-chrome-extensions module files. Might be needed if
	 * JavaScript bundlers like Webpack are used in your build process.
	 *
	 * @deprecated See "Packaging the preload script" in the readme.
	 */
	modulePath?: string;
}

const sessionMap = new WeakMap<Electron.Session, ElectronChromeExtensions>();

/**
 * Provides an implementation of various Chrome extension APIs to a session.
 */
export class ElectronChromeExtensions extends EventEmitter {
	/** Retrieve an instance of this class associated with the given session. */
	static fromSession(session: Electron.Session) {
		return sessionMap.get(session);
	}

	/**
	 * Sanitizes extension manifests on disk and clears Chromium's cached
	 * extension data (Preferences + Service Worker dirs) when manifests changed.
	 *
	 * Call once at process startup, before any session is created.
	 */
	static prepareUserData(userDataPath: string): void {
		try {
			const extensionsPath = path.join(userDataPath, "Extensions");
			const manifestsChanged = sanitizeExtensionManifests(extensionsPath);

			if (manifestsChanged) {
				ElectronChromeExtensions.clearExtensionCache(userDataPath);
			}
		} catch {}
	}

	private static clearExtensionCache(userDataPath: string): void {
		const dirs: string[] = [userDataPath];
		const partitionsDir = path.join(userDataPath, "Partitions");

		if (fs.existsSync(partitionsDir)) {
			for (const name of fs.readdirSync(partitionsDir)) {
				dirs.push(path.join(partitionsDir, name));
			}
		}

		for (const dir of dirs) {
			const prefsPath = path.join(dir, "Preferences");

			if (fs.existsSync(prefsPath)) {
				try {
					const prefs = JSON.parse(fs.readFileSync(prefsPath, "utf-8"));

					if (prefs.extensions) {
						delete prefs.extensions;
						fs.writeFileSync(prefsPath, JSON.stringify(prefs));
					}
				} catch {}
			}

			const swDir = path.join(dir, "Service Worker");

			if (fs.existsSync(swDir)) {
				fs.rmSync(swDir, { recursive: true, force: true });
			}
		}
	}

	/**
	 * Handles the 'crx://' protocol in the session.
	 *
	 * This is required to display <browser-action-list> extension icons.
	 */
	static handleCRXProtocol(session: Electron.Session) {
		if (session.protocol.isProtocolHandled("crx")) {
			session.protocol.unhandle("crx");
		}

		session.protocol.handle("crx", function handleCRXRequest(request) {
			let url;

			try {
				url = new URL(request.url);
			} catch {
				return new Response("Invalid URL", { status: 404 });
			}

			const partition = url?.searchParams.get("partition") || "_self";

			const remoteSession =
				partition === "_self" ? session : resolvePartition(partition);

			const extensions = ElectronChromeExtensions.fromSession(remoteSession);

			if (!extensions) {
				return new Response(
					`ElectronChromeExtensions not found for "${partition}"`,
					{
						status: 404,
					},
				);
			}

			return extensions.api.browserAction.handleCRXRequest(request);
		});
	}

	private ctx: ExtensionContext;

	private api: {
		browserAction: BrowserActionAPI;
		contextMenus: ContextMenusAPI;
		commands: CommandsAPI;
		cookies: CookiesAPI;
		notifications: NotificationsAPI;
		permissions: PermissionsAPI;
		runtime: RuntimeAPI;
		tabs: TabsAPI;
		webNavigation: WebNavigationAPI;
		windows: WindowsAPI;
	};

	constructor(opts: ChromeExtensionOptions) {
		super();

		const {
			license,
			session = electronSession.defaultSession,
			...impl
		} = opts || {};

		checkVersion();
		checkLicense(license);

		if (sessionMap.has(session)) {
			throw new Error(
				`Extensions instance already exists for the given session`,
			);
		}

		sessionMap.set(session, this);

		const router = new ExtensionRouter(session);
		const store = new ExtensionStore(impl);

		this.ctx = {
			emit: this.emit.bind(this),
			on: this.on.bind(this),
			router,
			session,
			store,
		};

		this.api = {
			browserAction: new BrowserActionAPI(this.ctx),
			contextMenus: new ContextMenusAPI(this.ctx),
			commands: new CommandsAPI(this.ctx),
			cookies: new CookiesAPI(this.ctx),
			notifications: new NotificationsAPI(this.ctx),
			permissions: new PermissionsAPI(this.ctx),
			runtime: new RuntimeAPI(this.ctx),
			tabs: new TabsAPI(this.ctx),
			webNavigation: new WebNavigationAPI(this.ctx),
			windows: new WindowsAPI(this.ctx),
		};

		this.listenForExtensions();
		this.prependPreload();

		registerShimHandler(this.ctx);
		registerShimHandlerForSession(this.ctx);

		/**
		 * `set-popup` — emitted by the host app to imperatively override the popup URL
		 * for a specific extension's browser action (e.g. to swap UIs based on auth state).
		 * Delegates directly to `BrowserActionAPI.setPopupUrl`.
		 */
		this.on("set-popup", (extensionId: string, popup: string) => {
			this.api.browserAction.setPopupUrl(extensionId, popup);
		});

		/**
		 * `browser-action-clicked` — emitted when the user clicks an extension's toolbar
		 * button and no popup URL is currently set for that extension.
		 *
		 * The handler resolves the best available HTML entry point from the extension
		 * manifest (`options_ui.page` → `options_page` → `action.default_popup`) and
		 * falls back to `findExtensionPage()` for extensions that ship a top-level HTML
		 * file without explicitly declaring it.
		 *
		 * Opens the extension's full-page UI as a tab. This handles extensions that have
		 * no popup UI for certain states (e.g. NordPass when unauthenticated delegates
		 * login to app.html).
		 */
		this.on("browser-action-clicked", (extensionId: string) => {
			const ext = session.extensions.getExtension(extensionId);

			if (!ext) {
				return;
			}

			const page =
				ext.manifest.options_ui?.page ||
				ext.manifest.options_page ||
				ext.manifest.action?.default_popup ||
				this.findExtensionPage(ext.path);

			if (!page) {
				return;
			}

			const url = `chrome-extension://${extensionId}/${page}`;
			this.ctx.store.createTab({ url }).catch(() => {});
		});
	}

	/**
	 * Subscribes to the session's `extension-loaded` event so that each newly loaded
	 * extension has its manifest read and registered with the ECE store.
	 *
	 * This covers both extensions loaded before and after this instance is constructed —
	 * Electron replays the event for already-loaded extensions when a listener is added.
	 */
	private listenForExtensions() {
		const sessionExtensions = this.ctx.session.extensions || this.ctx.session;

		sessionExtensions.addListener("extension-loaded", (_event, extension) => {
			readLoadedExtensionManifest(this.ctx, extension);
		});
	}

	/**
	 * Resolves the directory that contains the compiled preload scripts (`frame.js`, `sw.js`).
	 *
	 * The path cannot be derived from `__dirname` alone because bundlers (e.g. electron-vite)
	 * may inline this package's CJS entry point into the app's own output bundle, making
	 * `__dirname` point to the app's output directory rather than the package's `dist/` tree.
	 *
	 * Resolution order:
	 * 1. `__dirname/../preloads/` — standard non-bundled layout where `__dirname` is `dist/cjs/`.
	 * 2. `createRequire` from the app's working directory — resolves the package entry via Node's
	 *    module resolution and derives the preloads directory from there.  Used when the main
	 *    process is bundled but the package is still installed in `node_modules`.
	 * 3. `__dirname/preloads/` — final fallback for layouts where the preloads are copied
	 *    alongside the app's own output files.
	 *
	 * The first candidate directory that contains `sw.js` wins.
	 */
	private static resolvePreloadsDir(): string {
		const candidates = [
			// When not bundled: __dirname is dist/cjs/, preloads at dist/preloads/
			path.join(__dirname, "..", "preloads"),

			// When bundled by electron-vite: resolve via the app root.
			// In dev, app.getAppPath() is the project root (e.g. apps/veil/).
			// In production, it returns the ASAR path (e.g. .../app.asar).
			// For packaged apps, preloads must be asarUnpack'd — the real files
			// live at app.asar.unpacked/... so we try both the ASAR path and
			// its .unpacked sibling.
			...(() => {
				try {
					const appRoot = app.getAppPath();

					const mod = createRequire(`${appRoot}/`).resolve(
						"@pane/electron-chrome-extensions",
					);

					const preloads = path.join(path.dirname(mod), "..", "preloads");

					// For ASAR: also try the unpacked sibling where asarUnpack extracts files
					const unpacked = preloads.replace(
						/app\.asar([/\\])/,
						"app.asar.unpacked$1",
					);

					return unpacked !== preloads ? [unpacked, preloads] : [preloads];
				} catch {
					return [];
				}
			})(),

			// Fallback: preloads copied alongside the app's output
			path.join(__dirname, "preloads"),
		];

		for (const dir of candidates) {
			if (existsSync(path.join(dir, "sw.js"))) {
				return dir;
			}
		}

		return candidates[0];
	}

	/**
	 * Registers the ECE preload scripts on the session so that Chrome extension APIs
	 * are available in every context that needs them.
	 *
	 * Two scripts are registered:
	 * - **`frame.js`** (`type: 'frame'`) — injected into every renderer frame, including
	 *   extension popup and options pages.
	 * - **`sw.js`** (`type: 'service-worker'`) — injected into extension service-worker
	 *   contexts so background scripts can call `chrome.*` APIs.
	 *
	 * Both are prepended via `session.registerPreloadScript`, which requires Electron ≥ 35.
	 */
	private prependPreload() {
		const { session } = this.ctx;
		const preloadsDir = ElectronChromeExtensions.resolvePreloadsDir();

		if ("registerPreloadScript" in session) {
			const apiPreload = path.join(
				preloadsDir,
				"..",
				"chrome-extension-api.preload.js",
			);

			// SW: sw.js MUST run before crx-api-sw so it can cache uncorrupted
			// native chrome APIs before injectExtensionAPIs corrupts the V8 Proxy.
			session.registerPreloadScript({
				id: "crx-sw",
				type: "service-worker",
				filePath: path.join(preloadsDir, "sw.js"),
			});

			session.registerPreloadScript({
				id: "crx-api-sw",
				type: "service-worker",
				filePath: apiPreload,
			});

			// Frame: order doesn't matter (chrome is a regular object, not a V8 Proxy)
			session.registerPreloadScript({
				id: "crx-api-frame",
				type: "frame",
				filePath: apiPreload,
			});

			session.registerPreloadScript({
				id: "crx-frame",
				type: "frame",
				filePath: path.join(preloadsDir, "frame.js"),
			});
		}
	}

	/**
	 * Searches an extension's root directory for a conventional HTML entry point.
	 *
	 * The candidates are checked in priority order: `app.html`, `popup.html`,
	 * `main.html`, `index.html`.  The first file that exists on disk is returned as
	 * a relative path suitable for constructing a `chrome-extension://` URL.
	 * Returns `null` if none of the candidates exist.
	 *
	 * @param extPath Absolute path to the unpacked extension directory.
	 */
	private findExtensionPage(extPath: string): string | null {
		for (const name of ["app.html", "popup.html", "main.html", "index.html"]) {
			if (existsSync(path.join(extPath, name))) {
				return name;
			}
		}

		return null;
	}

	private checkWebContentsArgument(wc: Electron.WebContents) {
		if (this.ctx.session !== wc.session) {
			throw new TypeError(
				"Invalid WebContents argument. Its session must match the session provided to ElectronChromeExtensions constructor options.",
			);
		}
	}

	/**
	 * Tears down all resources owned by this ECE instance.
	 *
	 * - Cancels any pending `chrome.alarms` timers for the session.
	 * - Clears any `chrome.idle` polling intervals for the session.
	 * - Removes the session entry from the `sessionMap` weak-map so the instance
	 *   can be garbage-collected and a new one can be created for the same session
	 *   if needed.
	 *
	 * Call this when the associated browser profile is destroyed.
	 */
	destroy() {
		destroyAlarms(this.ctx.session);
		destroyIdle(this.ctx.session);
		sessionMap.delete(this.ctx.session);
	}

	/** Add webContents to be tracked as a tab. */
	addTab(tab: Electron.WebContents, window: Electron.BaseWindow) {
		this.checkWebContentsArgument(tab);
		this.ctx.store.addTab(tab, window);
	}

	/** Remove webContents from being tracked as a tab. */
	removeTab(tab: Electron.WebContents) {
		this.checkWebContentsArgument(tab);
		this.ctx.store.removeTab(tab);
	}

	/** Set the popup URL for an extension's browser action. */
	setPopup(extensionId: string, popup: string) {
		this.api.browserAction.setPopupUrl(extensionId, popup);
	}

	/** Notify extension system that the active tab has changed. */
	selectTab(tab: Electron.WebContents) {
		this.checkWebContentsArgument(tab);

		if (this.ctx.store.tabs.has(tab)) {
			this.api.tabs.onActivated(tab.id);
		}
	}

	/**
	 * Add webContents to be tracked as an extension host which will receive
	 * extension events when a chrome-extension:// resource is loaded.
	 *
	 * This is usually reserved for extension background pages and popups, but
	 * can also be used in other special cases.
	 *
	 * @deprecated Extension hosts are now tracked lazily when they send
	 * extension IPCs to the main process.
	 */
	addExtensionHost(_host: Electron.WebContents) {
		console.warn("ElectronChromeExtensions.addExtensionHost() is deprecated");
	}

	/**
	 * Get collection of menu items managed by the `chrome.contextMenus` API.
	 * @see https://developer.chrome.com/extensions/contextMenus
	 */
	getContextMenuItems(
		webContents: Electron.WebContents,
		params: Electron.ContextMenuParams,
	) {
		this.checkWebContentsArgument(webContents);

		return this.api.contextMenus.buildMenuItemsForParams(webContents, params);
	}

	/**
	 * Gets map of special pages to extension override URLs.
	 *
	 * @see https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/manifest.json/chrome_url_overrides
	 */
	getURLOverrides(): Record<string, string> {
		return this.ctx.store.urlOverrides;
	}

	/**
	 * Handles the 'crx://' protocol in the session.
	 *
	 * @deprecated Call `ElectronChromeExtensions.handleCRXProtocol(session)`
	 * instead. The CRX protocol is no longer one-to-one with
	 * ElectronChromeExtensions instances. Instead, it should now be handled only
	 * on the sessions where <browser-action-list> extension icons will be shown.
	 */
	handleCRXProtocol(_session: Electron.Session) {
		throw new Error(
			"extensions.handleCRXProtocol(session) is deprecated, call ElectronChromeExtensions.handleCRXProtocol(session) instead.",
		);
	}

	/**
	 * Add extensions to be visible as an extension action button.
	 *
	 * @deprecated Not needed in Electron >=12.
	 */
	addExtension(extension: Electron.Extension) {
		console.warn("ElectronChromeExtensions.addExtension() is deprecated");
		this.api.browserAction.processExtension(extension);
	}

	/**
	 * Remove extensions from the list of visible extension action buttons.
	 *
	 * @deprecated Not needed in Electron >=12.
	 */
	removeExtension(extension: Electron.Extension) {
		console.warn("ElectronChromeExtensions.removeExtension() is deprecated");
		this.api.browserAction.removeActions(extension.id);
	}
}
