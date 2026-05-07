import path from "node:path";
import { app, type BaseWindow, type WebContents } from "electron";

import { navigationStore, Page } from "../stores/navigation-store";
import { profileStore } from "../stores/profile-store";
import { settingsStore } from "../stores/settings-store";
import { tabStore } from "../stores/tab-store";
import { autoDetectBrowser } from "./detect-browser";
import type { FindEmitter } from "./emitters/find-emitter";
import { ExtensionInstaller } from "./extensions";
import { Profile } from "./profile/profile";

export class Pane {
	readonly extensions: ExtensionInstaller;
	readonly profiles = new Map<string, Profile>();
	readonly tabStore = tabStore;
	private readonly extensionsPath: string;
	private readonly tabIndex = new Map<string, string>();
	private readonly unsubscribeNavigation: () => void;

	constructor(
		private readonly mainWindow: BaseWindow,
		private readonly findEmitter: FindEmitter,
	) {
		this.extensionsPath = path.join(app.getPath("userData"), "Extensions");
		this.extensions = new ExtensionInstaller(this, this.extensionsPath);

		let previousPage = navigationStore.getState().page;

		this.unsubscribeNavigation = navigationStore.subscribe((state) => {
			if (state.page === previousPage) {
				return;
			}

			previousPage = state.page;

			if (state.page === Page.BROWSER) {
				const { activeProfileId } = tabStore.getState();

				if (activeProfileId) {
					this.getProfile(activeProfileId)?.tabs.showActive();
				}
			} else {
				this.hideAllTabs();
			}
		});
	}

	destroy(): void {
		this.unsubscribeNavigation();

		for (const profile of this.profiles.values()) {
			profile.tabs.destroyAll();
		}
	}

	createProfile(
		input: Parameters<ReturnType<typeof profileStore.getState>["create"]>[0],
	): Profile {
		const id = profileStore.getState().create(input);

		return this.addProfile(id);
	}

	getProfile(id: string): Profile | undefined {
		return this.profiles.get(id);
	}

	getOrCreateProfile(id: string): Profile {
		return this.profiles.get(id) ?? this.addProfile(id);
	}

	private addProfile(id: string): Profile {
		const profile = new Profile(
			id,
			this.mainWindow,
			this.extensionsPath,
			this.findEmitter,
			(tabId, profileId) => this.tabIndex.set(tabId, profileId),
			(tabId) => this.tabIndex.delete(tabId),
		);

		this.profiles.set(id, profile);

		return profile;
	}

	registerTab(tabId: string, profileId: string): void {
		this.tabIndex.set(tabId, profileId);
	}

	unregisterTab(tabId: string): void {
		this.tabIndex.delete(tabId);
	}

	getProfileForTab(tabId: string): Profile | undefined {
		const profileId = this.tabIndex.get(tabId);

		if (profileId) {
			return this.profiles.get(profileId);
		}

		const profileData = profileStore
			.getState()
			.profiles.find((profile) => profile.tabs.some((tab) => tab.id === tabId));

		return profileData ? this.profiles.get(profileData.id) : undefined;
	}

	removeProfile(id: string): void {
		this.profiles.get(id)?.destroy();
		this.profiles.delete(id);
		profileStore.getState().remove(id);
	}

	restore(): void {
		if (!settingsStore.getState().settings.chromiumPath) {
			autoDetectBrowser(settingsStore.getState().update);
		}

		for (const data of profileStore.getState().profiles) {
			this.addProfile(data.id);
		}

		this.extensions.checkForUpdates().catch((error) => {
			console.error("[CWS] Update check failed:", error);
		});

		if (process.env.PANE_DEV_EXTENSIONS === "1") {
			const devExtensions = [
				"eimadpbcbfnmbkopoojfekhnkhdbieeh", // Dark Reader
				"aeblfdkhhhdcdjpifhhbdiojplfjncoa", // 1Password
				"eiaeiblijfjekdanodkjadfinkhbfgcd", // NordPass
			];

			this.extensions.getInstalled().then((installed) => {
				const installedIds = new Set(
					installed.map((extension) => extension.id),
				);

				for (const id of devExtensions) {
					if (!installedIds.has(id)) {
						this.extensions
							.install(id)
							.then(() => console.log(`[DEV] Auto-installed extension ${id}`))
							.catch((error) =>
								console.error(`[DEV] Failed to auto-install ${id}:`, error),
							);
					}
				}
			});
		}
	}

	private getActiveProfile(): Profile | undefined {
		const { activeProfileId } = tabStore.getState();

		return activeProfileId ? this.profiles.get(activeProfileId) : undefined;
	}

	getActiveTabContents(): WebContents | undefined {
		return this.getActiveProfile()?.tabs.getActiveWebContents();
	}

	hideAllTabs(): void {
		for (const profile of this.profiles.values()) {
			profile.tabs.hideAll();
		}
	}

	resizeAllTabs(): void {
		for (const profile of this.profiles.values()) {
			profile.tabs.resizeAll();
		}
	}

	navigateToBrowser(): void {
		navigationStore.getState().navigate(Page.BROWSER);
	}

	switchToTabByIndex(index: number): void {
		const { activeProfileId } = tabStore.getState();

		if (!activeProfileId) {
			return;
		}

		const profile = profileStore
			.getState()
			.profiles.find((profile) => profile.id === activeProfileId);

		if (!profile) {
			return;
		}

		const tab = profile.tabs[index];

		if (!tab) {
			return;
		}

		this.hideAllTabs();
		this.getOrCreateProfile(activeProfileId).tabs.activate(tab.id);
		this.navigateToBrowser();
	}
}
