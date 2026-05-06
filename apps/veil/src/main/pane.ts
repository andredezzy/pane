import path from "node:path";
import { app, type BaseWindow, type WebContents } from "electron";

import { profileStore } from "../stores/profile-store";
import { tabStore } from "../stores/tab-store";
import { ExtensionInstaller } from "./extension-installer";
import { Profile } from "./profile/profile";

export class Pane {
	readonly extensions: ExtensionInstaller;
	readonly profiles = new Map<string, Profile>();
	private readonly extensionsPath: string;
	private readonly tabIndex = new Map<string, string>();

	constructor(private readonly mainWindow: BaseWindow) {
		this.extensionsPath = path.join(app.getPath("userData"), "Extensions");
		this.extensions = new ExtensionInstaller(this, this.extensionsPath);
	}

	createProfile(
		input: Parameters<ReturnType<typeof profileStore.getState>["create"]>[0],
	): Profile {
		const id = profileStore.getState().create(input);

		return this.initProfile(id);
	}

	getProfile(id: string): Profile | undefined {
		return this.profiles.get(id);
	}

	getOrCreateProfile(id: string): Profile {
		return this.profiles.get(id) ?? this.initProfile(id);
	}

	private initProfile(id: string): Profile {
		const profile = new Profile(
			id,
			this.mainWindow,
			this.extensionsPath,
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
			.profiles.find((p) => p.tabs.some((t) => t.id === tabId));

		return profileData ? this.profiles.get(profileData.id) : undefined;
	}

	removeProfile(id: string): void {
		this.profiles.get(id)?.destroy();
		this.profiles.delete(id);
		profileStore.getState().remove(id);
	}

	restore(): void {
		for (const data of profileStore.getState().profiles) {
			this.initProfile(data.id);
		}

		this.extensions.checkForUpdates().catch((err) => {
			console.error("[CWS] Update check failed:", err);
		});

		if (process.env.PANE_DEV_EXTENSIONS === "1") {
			const devExtensions = [
				"eimadpbcbfnmbkopoojfekhnkhdbieeh", // Dark Reader
				"aeblfdkhhhdcdjpifhhbdiojplfjncoa", // 1Password
				"eiaeiblijfjekdanodkjadfinkhbfgcd", // NordPass
			];

			this.extensions.getInstalled().then((installed) => {
				const installedIds = new Set(installed.map((e) => e.id));
				for (const id of devExtensions) {
					if (!installedIds.has(id)) {
						this.extensions
							.install(id)
							.then(() => console.log(`[DEV] Auto-installed extension ${id}`))
							.catch((err) =>
								console.error(`[DEV] Failed to auto-install ${id}:`, err),
							);
					}
				}
			});
		}
	}

	getActiveTabContents(): WebContents | undefined {
		const { activeProfileId } = tabStore.getState();

		if (!activeProfileId) {
			return undefined;
		}

		return this.profiles.get(activeProfileId)?.tabs.getActiveWebContents();
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
}
