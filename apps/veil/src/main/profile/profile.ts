import {
	ElectronChromeExtensions,
	ExtensionRuntime,
} from "@pane/electron-chrome-extensions";
import { app, type BaseWindow, session } from "electron";
import { extensionStore } from "../../stores/extension-store";
import { type BrowserProfile, profileStore } from "../../stores/profile-store";
import type { FindEmitter } from "../emitters/find-emitter";
import {
	clientHintHeaders,
	deriveClientHints,
	reconcileChromeVersion,
} from "./client-hints";
import {
	cleanupFingerprintPreload,
	generateFingerprintPreload,
} from "./fingerprint-preload";
import { clearGoogleSession } from "./google/sign-out";
import { ProfileTabs, type TabHost } from "./profile-tabs";
import { ProxyRelay } from "./proxy-relay";
import { testProxyConnection } from "./proxy-test";

export function profileSession(id: string): Electron.Session {
	return session.fromPartition(`persist:profile-${id}`);
}

// Electron names no type for its app "login" listener, so this mirrors the
// signature in its typings (electron.d.ts, the app `on(event: 'login', …)`
// overload). Spelled out rather than `any[]`, which loses every argument.
type ProxyLoginHandler = (
	event: Electron.Event,
	webContents: Electron.WebContents,
	details: Electron.AuthenticationResponseDetails,
	authInfo: Electron.AuthInfo,
	callback: (username?: string, password?: string) => void,
) => void;

export class Profile implements TabHost {
	readonly session: Electron.Session;
	readonly ece: ElectronChromeExtensions;
	readonly tabs: ProfileTabs;
	readonly extensions: ExtensionRuntime;
	readonly proxyReady: Promise<boolean>;
	private proxyLoginHandler?: ProxyLoginHandler;
	private proxyRelay?: ProxyRelay;
	private readonly fingerprintPreloadIds: string[] = [];

	constructor(
		readonly id: string,
		private readonly mainWindow: BaseWindow,
		extensionsPath: string,
		private readonly findEmitter: FindEmitter,
		private readonly tabRegistered?: (tabId: string, profileId: string) => void,
		private readonly tabUnregistered?: (tabId: string) => void,
	) {
		this.session = profileSession(id);

		// navigator.clipboard.writeText() is gated by a synchronous "clipboard-
		// sanitized-write" permission check that Electron denies by default, which
		// silently breaks copy buttons — including inside an extension's popup (it
		// runs on this same profile session). Approve ONLY clipboard WRITE checks;
		// everything else (clipboard-read, hid, serial, usb, midi, …) stays denied so
		// a page can't silently read the clipboard or reach a device without a prompt.
		this.session.setPermissionCheckHandler(
			(_webContents, permission) => permission === "clipboard-sanitized-write",
		);

		const profileData = profileStore
			.getState()
			.profiles.find((profile) => profile.id === id);

		const fingerprint = profileData?.fingerprint;

		// Present the profile's claimed identity as the single source of truth.
		// Using the fingerprint's User-Agent (and matching Accept-Language) keeps
		// the HTTP UA consistent with the spoofed navigator.platform / languages —
		// a mismatch (e.g. a macOS UA over a Win32 platform) is exactly what trips
		// Google's abuse checks on accounts.google.com (error #2014).
		//
		// The real engine UA is the source of truth for the Chrome VERSION: a stored
		// fingerprint pins the version at creation time, so it goes stale on every
		// Electron/Chromium bump, and a stale claim (Chrome 136 on a Chromium 146
		// engine) is a contradiction Cloudflare Turnstile detects by feature-probing
		// the runtime. Reconcile the claimed UA against the live engine, then feed
		// the one reconciled fingerprint to the UA, the client-hint headers, AND the
		// preload's navigator.userAgentData spoof so all three surfaces agree.
		const realUserAgent = this.session
			.getUserAgent()
			.replace(/\s*Electron\/\S+/g, "")
			.replace(/\s*@?pane\/\S+/gi, "")
			.replace(/\s{2,}/g, " ");

		const userAgent = reconcileChromeVersion(
			fingerprint?.userAgent ?? realUserAgent,
			realUserAgent,
		);

		const reconciledFingerprint = fingerprint
			? { ...fingerprint, userAgent }
			: undefined;

		this.session.setUserAgent(userAgent, fingerprint?.languages.join(","));

		// Align the Sec-CH-UA-* client-hint headers with the fingerprint (the same
		// source of truth as the navigator.userAgentData spoof). Computed once and
		// applied only to hints Chromium actually sends — never added — so no hint
		// the browser suppressed can leak.
		const clientHintOverrides = reconciledFingerprint
			? clientHintHeaders(deriveClientHints(reconciledFingerprint))
			: null;

		this.session.webRequest.onBeforeSendHeaders((details, callback) => {
			const headers = { ...details.requestHeaders };

			for (const key of Object.keys(headers)) {
				const lowerKey = key.toLowerCase();
				const value = headers[key];

				if (typeof value !== "string" || !lowerKey.startsWith("sec-ch-ua")) {
					continue;
				}

				const override = clientHintOverrides?.[lowerKey];

				if (override !== undefined) {
					headers[key] = override;
				} else if (/electron|pane/i.test(value)) {
					headers[key] = value
						.split(",")
						.filter((brand) => !/electron|pane/i.test(brand))
						.join(",")
						.trim();
				}
			}

			callback({ requestHeaders: headers });
		});

		ElectronChromeExtensions.handleCRXProtocol(this.session);

		if (reconciledFingerprint) {
			const filePath = generateFingerprintPreload(id, reconciledFingerprint);

			// One preload for both worlds: a frame runs it in the isolated world (it
			// bridges into the page's main world); a service worker runs it directly.
			// Track the ids so shutdown can unregister them — the persistent session
			// outlives this Profile, so re-registering on reload would double-run it.
			for (const type of ["frame", "service-worker"] as const) {
				this.fingerprintPreloadIds.push(
					this.session.registerPreloadScript({ type, filePath }),
				);
			}
		}

		if (profileData?.proxy) {
			const p = profileData.proxy;
			const relay = new ProxyRelay(p);
			this.proxyRelay = relay;

			// Narrowed into a const, not asserted: TypeScript drops the narrowing of
			// a property read across a closure, but keeps it for a const binding.
			const { username } = p;

			if (username && !relay.needsRelay) {
				this.proxyLoginHandler = (
					event,
					webContents,
					_details,
					authInfo,
					callback,
				) => {
					if (authInfo.isProxy && webContents?.session === this.session) {
						event.preventDefault();
						callback(username, p.password ?? "");
					}
				};

				app.on("login", this.proxyLoginHandler);
			}

			this.proxyReady = relay
				.start()
				.then(() => this.session.setProxy({ proxyRules: relay.proxyUrl }))
				.then(() =>
					testProxyConnection(
						this.session,
						p.username && !relay.needsRelay
							? { username: p.username, password: p.password ?? "" }
							: undefined,
					),
				)
				.then((result) => {
					if (!result.success) {
						console.error(`[Profile ${id}] Proxy test failed:`, result.error);
					}

					return result.success;
				})
				.catch((error) => {
					console.error(`[Profile ${id}] Proxy failed:`, error);

					return false;
				});
		} else {
			this.proxyReady = Promise.resolve(true);
		}

		this.extensions = new ExtensionRuntime({
			profileId: id,
			session: this.session,
			extensionsPath,
			onExtensionLoaded: (profileId, ext) => {
				extensionStore.getState().addExtension(profileId, ext);
			},
		});

		this.ece = new ElectronChromeExtensions({
			license: "GPL-3.0",
			session: this.session,
			createTab: async (details) => {
				const view = this.tabs.openForExtension(details.url || "about:blank");

				return [view.webContents, this.mainWindow];
			},
			selectTab: (webContents) => this.tabs.activateByWebContents(webContents),
			removeTab: (webContents) => this.tabs.destroyByWebContents(webContents),
			// chrome.windows.create opens a TAB on this tabbed host, so chrome.windows
			// .remove must not fall through to destroying the real BrowserWindow (which
			// would close the user's entire window). No-op: the tab is closed like any
			// other tab, never via the window API.
			removeWindow: () => {
				// Intentionally does nothing — see comment above.
			},
		});

		this.tabs = new ProfileTabs(this, mainWindow, this.findEmitter);
	}

	get data(): BrowserProfile {
		const profile = profileStore
			.getState()
			.profiles.find((profile) => profile.id === this.id);

		if (!profile) {
			throw new Error(`Profile ${this.id} not found in store`);
		}

		return profile;
	}

	async signOutGoogle(): Promise<number> {
		const count = await clearGoogleSession(this.session);

		this.tabs.reloadGoogleTabs();

		return count;
	}

	onTabOpened(tabId: string): void {
		this.tabRegistered?.(tabId, this.id);
	}

	onTabClosed(tabId: string): void {
		this.tabUnregistered?.(tabId);
	}

	shutdown(): void {
		cleanupFingerprintPreload(this.id);

		for (const scriptId of this.fingerprintPreloadIds) {
			this.session.unregisterPreloadScript(scriptId);
		}

		if (this.proxyLoginHandler) {
			app.removeListener("login", this.proxyLoginHandler);
		}

		this.proxyRelay?.stop();
		this.tabs.destroyAll();
		this.ece.destroy();
	}

	destroy(): void {
		this.shutdown();

		extensionStore.getState().clearProfile(this.id);
	}
}
