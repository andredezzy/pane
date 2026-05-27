import {
	ElectronChromeExtensions,
	ExtensionRuntime,
} from "@pane/electron-chrome-extensions";
import { type BaseWindow, app, session } from "electron";
import { extensionStore } from "../../stores/extension-store";
import { type BrowserProfile, profileStore } from "../../stores/profile-store";
import type { FindEmitter } from "../emitters/find-emitter";
import {
	cleanupFingerprintPreload,
	generateFingerprintPreload,
} from "./fingerprint-preload";
import { ProfileTabs, type TabHost } from "./profile-tabs";
import { ProxyRelay } from "./proxy-relay";
import { testProxyConnection } from "./proxy-test";

export class Profile implements TabHost {
	readonly session: Electron.Session;
	readonly ece: ElectronChromeExtensions;
	readonly tabs: ProfileTabs;
	readonly extensions: ExtensionRuntime;
	readonly proxyReady: Promise<boolean>;
	private proxyLoginHandler?: (...args: any[]) => void;
	private proxyRelay?: ProxyRelay;

	constructor(
		readonly id: string,
		private readonly mainWindow: BaseWindow,
		extensionsPath: string,
		private readonly findEmitter: FindEmitter,
		private readonly tabRegistered?: (tabId: string, profileId: string) => void,
		private readonly tabUnregistered?: (tabId: string) => void,
	) {
		this.session = session.fromPartition(`persist:profile-${id}`);

		const profileData = profileStore
			.getState()
			.profiles.find((profile) => profile.id === id);

		this.session.setUserAgent(
			this.session
				.getUserAgent()
				.replace(/\s*Electron\/\S+/g, "")
				.replace(/\s*@?pane\/\S+/gi, "")
				.replace(/\s{2,}/g, " "),
		);

		this.session.webRequest.onBeforeSendHeaders((details, callback) => {
			const headers = { ...details.requestHeaders };

			for (const key of Object.keys(headers)) {
				if (
					key.toLowerCase().startsWith("sec-ch-ua") &&
					typeof headers[key] === "string" &&
					/electron|pane/i.test(headers[key] as string)
				) {
					headers[key] = (headers[key] as string)
						.split(",")
						.filter((brand) => !/electron|pane/i.test(brand))
						.join(",")
						.trim();
				}
			}

			callback({ requestHeaders: headers });
		});

		ElectronChromeExtensions.handleCRXProtocol(this.session);

		if (profileData?.fingerprint) {
			const fingerprintPreloadPath = generateFingerprintPreload(
				id,
				profileData.fingerprint,
			);

			(
				this.session as Electron.Session & {
					registerPreloadScript: (script: {
						type: string;
						filePath: string;
					}) => void;
				}
			).registerPreloadScript({
				type: "frame",
				filePath: fingerprintPreloadPath,
			});
		}

		if (profileData?.proxy) {
			const p = profileData.proxy;
			const relay = new ProxyRelay(p);
			this.proxyRelay = relay;

			if (p.username && !relay.needsRelay) {
				this.proxyLoginHandler = (event, webContents, _details, authInfo, callback) => {
					if (authInfo.isProxy && webContents?.session === this.session) {
						event.preventDefault();
						callback(p.username!, p.password ?? "");
					}
				};

				app.on("login", this.proxyLoginHandler);
			}

			this.proxyReady = relay
				.start()
				.then(() =>
					this.session.setProxy({ proxyRules: relay.proxyUrl }),
				)
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

	onTabOpened(tabId: string): void {
		this.tabRegistered?.(tabId, this.id);
	}

	onTabClosed(tabId: string): void {
		this.tabUnregistered?.(tabId);
	}

	shutdown(): void {
		cleanupFingerprintPreload(this.id);

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

function extractChromeVersion(userAgent: string): string {
	const match = userAgent.match(/Chrome\/([\d.]+)/);
	return match?.[1] ?? "130.0.0.0";
}
