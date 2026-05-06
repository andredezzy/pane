import {
	ElectronChromeExtensions,
	ExtensionRuntime,
} from "@pane/electron-chrome-extensions";
import { type BaseWindow, session } from "electron";
import { extensionStore } from "../../stores/extension-store";
import { type BrowserProfile, profileStore } from "../../stores/profile-store";
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
	constructor(
		readonly id: string,
		private readonly mainWindow: BaseWindow,
		extensionsPath: string,
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
			.profiles.find((p) => p.id === id);

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
			const fpPreloadPath = generateFingerprintPreload(
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
				filePath: fpPreloadPath,
			});
		}

		if (profileData?.proxy) {
			const p = profileData.proxy;

			this.session
				.setProxy({ proxyRules: `${p.proxyType}://${p.host}:${p.port}` })
				.catch((err) => {
					console.error(`[Profile ${id}] Proxy failed to apply:`, err);
				});
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
			selectTab: (wc) => this.tabs.activateByWebContents(wc),
			removeTab: (wc) => this.tabs.destroyByWebContents(wc),
		});

		this.tabs = new ProfileTabs(this, mainWindow);
	}

	get data(): BrowserProfile {
		const profile = profileStore
			.getState()
			.profiles.find((p) => p.id === this.id);

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
		this.tabs.closeAll();
		this.ece.destroy();
		extensionStore.getState().clearProfile(this.id);
	}
}
