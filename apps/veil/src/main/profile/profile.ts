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

export class Profile implements TabHost {
	readonly session: Electron.Session;
	readonly ece: ElectronChromeExtensions;
	readonly tabs: ProfileTabs;
	readonly extensions: ExtensionRuntime;
	private proxyLoginHandler?: (...args: any[]) => void;

	constructor(
		readonly id: string,
		private readonly mainWindow: BaseWindow,
		extensionsPath: string,
		private readonly findEmitter: FindEmitter,
		private readonly tabRegistered?: (tabId: string, profileId: string) => void,
		private readonly tabUnregistered?: (tabId: string) => void,
	) {
		this.session = session.fromPartition(`persist:profile-${id}`);

		this.session.setUserAgent(
			this.session
				.getUserAgent()
				.replace(/\s*Electron\/\S+/g, "")
				.replace(/\s*@?pane\/\S+/gi, "")
				.replace(/\s{2,}/g, " "),
		);

		const profileData = profileStore
			.getState()
			.profiles.find((profile) => profile.id === id);

		this.session.webRequest.onBeforeSendHeaders((details, callback) => {
			const headers = { ...details.requestHeaders };

			for (const key of Object.keys(headers)) {
				if (key.toLowerCase().startsWith("sec-ch-ua")) {
					delete headers[key];
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

			this.session
				.setProxy({
					proxyRules: `${p.proxyType.toLowerCase()}://${p.host}:${p.port}`,
				})
				.catch((error) => {
					console.error(`[Profile ${id}] Proxy failed to apply:`, error);
				});

			if (p.username) {
				this.proxyLoginHandler = (event, webContents, _details, authInfo, callback) => {
					if (authInfo.isProxy && webContents?.session === this.session) {
						event.preventDefault();
						callback(p.username!, p.password ?? "");
					}
				};

				app.on("login", this.proxyLoginHandler);
			}
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

	destroy(): void {
		cleanupFingerprintPreload(this.id);

		if (this.proxyLoginHandler) {
			app.removeListener("login", this.proxyLoginHandler);
		}

		this.tabs.closeAll();
		this.ece.destroy();

		extensionStore.getState().clearProfile(this.id);
	}
}
