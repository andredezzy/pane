import { profileStore } from "../stores/profile-store";
import { settingsStore } from "../stores/settings-store";
import { tabStore } from "../stores/tab-store";

const TICK_MS = 60_000;
const BYTES_PER_MB = 1024 * 1024;

interface SleepableTabs {
	loadedTabIds(): string[];
	isProtected(tabId: string): boolean;
	sleep(tabId: string): void;
	unloadAll(): void;
}

export interface TrimmableCache {
	size(): Promise<number>;
	trim(): Promise<void>;
}

// What the scheduler needs from a profile: tabs whose memory it can release,
// and a cache whose disk it can release. Narrow on purpose — it keeps this file
// free of Electron, so the whole scheduler runs against fakes in tests.
interface ProfileResources {
	readonly tabs: SleepableTabs;
	readonly cache: TrimmableCache;
}

interface ProfileHost {
	readonly profiles: ReadonlyMap<string, ProfileResources>;
}

// Puts background tabs to sleep, unloads hidden profiles once their settings
// timers elapse, and trims the cache of a profile that unloads over budget
// (CONTEXT.md: Sleep, Unload, Trim). Nothing sleeps while active, protected, or
// in a Keep loaded profile; a clock restarts when its tab or profile is
// re-activated, so recent interaction is protected by construction.
export class SleepScheduler {
	private readonly profileHiddenSince = new Map<string, number>();
	private readonly tabBackgroundSince = new Map<string, number>();
	private readonly trimming = new Set<string>();
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
		const { tabSleepAfterMinutes, profileUnloadAfterMinutes, cacheBudgetMB } =
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
				this.trim(profileId, profile.cache, cacheBudgetMB);

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

	// Fired, never awaited: tick() is synchronous and a slow disk must not stall
	// it. The in-flight set stops a later tick stacking a second trim on the same
	// profile. A failed trim is logged and dropped; the next unload retries.
	private trim(
		profileId: string,
		cache: TrimmableCache,
		budgetMB: number | null,
	): void {
		if (budgetMB === null || this.trimming.has(profileId)) {
			return;
		}

		this.trimming.add(profileId);

		cache
			.size()
			.then((bytes) =>
				bytes > budgetMB * BYTES_PER_MB ? cache.trim() : undefined,
			)
			.catch((error) => console.warn(`[Trim ${profileId}] Failed:`, error))
			.finally(() => this.trimming.delete(profileId));
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
