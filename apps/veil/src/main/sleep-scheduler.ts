import { profileStore } from "../stores/profile-store";
import { settingsStore } from "../stores/settings-store";
import { tabStore } from "../stores/tab-store";

const TICK_MS = 60_000;

interface SleepableTabs {
	loadedTabIds(): string[];
	isProtected(tabId: string): boolean;
	sleep(tabId: string): void;
	unloadAll(): void;
}

interface ProfileHost {
	readonly profiles: ReadonlyMap<string, { readonly tabs: SleepableTabs }>;
}

// Puts background tabs to sleep and unloads hidden profiles once their
// settings timers elapse (CONTEXT.md: Sleep, Unload). Nothing sleeps while
// active, protected, or in a Keep loaded profile; a clock restarts when its
// tab or profile is re-activated, so recent interaction is protected by
// construction.
export class SleepScheduler {
	private readonly profileHiddenSince = new Map<string, number>();
	private readonly tabBackgroundSince = new Map<string, number>();
	private readonly unsubscribe: () => void;
	private readonly timer: NodeJS.Timeout;

	constructor(private readonly host: ProfileHost) {
		let previousActive = {
			profileId: tabStore.getState().activeProfileId,
			tabId: tabStore.getState().activeTabId,
		};

		const now = Date.now();

		for (const profile of profileStore.getState().profiles) {
			if (profile.id !== previousActive.profileId) {
				this.profileHiddenSince.set(profile.id, now);
			}
		}

		this.unsubscribe = tabStore.subscribe((state) => {
			if (state.activeProfileId !== previousActive.profileId) {
				if (previousActive.profileId) {
					this.profileHiddenSince.set(previousActive.profileId, Date.now());
				}

				if (state.activeProfileId) {
					this.profileHiddenSince.delete(state.activeProfileId);
				}
			}

			if (state.activeTabId !== previousActive.tabId) {
				if (previousActive.tabId) {
					this.tabBackgroundSince.set(previousActive.tabId, Date.now());
				}

				if (state.activeTabId) {
					this.tabBackgroundSince.delete(state.activeTabId);
				}
			}

			previousActive = {
				profileId: state.activeProfileId,
				tabId: state.activeTabId,
			};
		});

		this.timer = setInterval(() => this.tick(), TICK_MS);
	}

	dispose(): void {
		clearInterval(this.timer);
		this.unsubscribe();
	}

	private tick(): void {
		const { tabSleepAfterMinutes, profileUnloadAfterMinutes } =
			settingsStore.getState().settings;

		const { activeProfileId, activeTabId } = tabStore.getState();
		const now = Date.now();

		for (const [profileId, profile] of this.host.profiles) {
			const loadedTabIds = profile.tabs.loadedTabIds();

			if (loadedTabIds.length === 0) {
				continue;
			}

			const data = profileStore
				.getState()
				.profiles.find((candidate) => candidate.id === profileId);

			if (!data || data.keepLoaded) {
				continue;
			}

			if (
				profileUnloadAfterMinutes !== null &&
				profileId !== activeProfileId &&
				this.elapsed(
					this.profileHiddenSince.get(profileId),
					profileUnloadAfterMinutes,
					now,
				) &&
				!loadedTabIds.some((tabId) => profile.tabs.isProtected(tabId))
			) {
				profile.tabs.unloadAll();

				// The hidden clock restarts on the next hide cycle: a tab appearing in
				// a still-hidden profile (e.g. opened by an extension) must not be
				// instantly re-unloaded against this stale stamp.
				this.profileHiddenSince.delete(profileId);

				continue;
			}

			if (tabSleepAfterMinutes === null) {
				continue;
			}

			for (const tabId of loadedTabIds) {
				if (
					tabId !== activeTabId &&
					this.elapsed(
						this.tabBackgroundSince.get(tabId),
						tabSleepAfterMinutes,
						now,
					) &&
					!profile.tabs.isProtected(tabId)
				) {
					profile.tabs.sleep(tabId);
					this.tabBackgroundSince.delete(tabId);
				}
			}
		}

		this.pruneTabStamps();
	}

	private elapsed(
		since: number | undefined,
		minutes: number,
		now: number,
	): boolean {
		return since !== undefined && now - since >= minutes * 60_000;
	}

	// Closed tabs never re-enter the loaded set, so their stamps would pile up
	// for the lifetime of the window without this sweep.
	private pruneTabStamps(): void {
		const loaded = new Set<string>();

		for (const profile of this.host.profiles.values()) {
			for (const tabId of profile.tabs.loadedTabIds()) {
				loaded.add(tabId);
			}
		}

		for (const tabId of this.tabBackgroundSince.keys()) {
			if (!loaded.has(tabId)) {
				this.tabBackgroundSince.delete(tabId);
			}
		}
	}
}
