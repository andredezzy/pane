import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

import { ProfileColor } from "../constants/profile-colors";
import { profileStore } from "../stores/profile-store";
import { settingsStore } from "../stores/settings-store";
import { tabStore } from "../stores/tab-store";
import { SleepScheduler } from "./sleep-scheduler";

const files = vi.hoisted(() => new Map<string, string>());

vi.mock("../stores/middlewares/fs-storage", () => ({
	fsStorage: {
		getItem: (name: string) => files.get(name) ?? null,
		setItem: (name: string, value: string) => {
			files.set(name, value);
		},
		removeItem: (name: string) => {
			files.delete(name);
		},
	},
}));

function fakeTabs(tabIds: string[] = ["tab-1"]) {
	const loaded = new Set(tabIds);

	return {
		loadedTabIds: vi.fn<() => string[]>(() => [...loaded]),
		isProtected: vi.fn<(tabId: string) => boolean>(() => false),
		sleep: vi.fn<(tabId: string) => void>((tabId) => {
			loaded.delete(tabId);
		}),
		unloadAll: vi.fn<() => void>(() => {
			loaded.clear();
		}),
	};
}

function fakeCache(sizeMB = 0) {
	return {
		size: vi.fn<() => Promise<number>>(async () => sizeMB * 1024 * 1024),
		trim: vi.fn<() => Promise<void>>(async () => {}),
	};
}

function createProfile(input?: { keepLoaded?: boolean }): string {
	return profileStore.getState().create({
		name: "profile",
		color: ProfileColor.BLUE,
		group: null,
		fingerprint: null,
		proxy: null,
		keepLoaded: input?.keepLoaded,
	});
}

const SIXTEEN_MINUTES = 16 * 60_000;
const THIRTY_ONE_MINUTES = 31 * 60_000;

describe("SleepScheduler", () => {
	let scheduler: SleepScheduler | undefined;

	beforeEach(() => {
		vi.useFakeTimers();
		profileStore.setState({ profiles: [] });
		tabStore.getState().setActiveTab(null, null);

		settingsStore.getState().update({
			tabSleepAfterMinutes: 15,
			profileUnloadAfterMinutes: 30,
			cacheBudgetMB: 300,
		});
	});

	afterEach(() => {
		scheduler?.dispose();
		scheduler = undefined;
		vi.useRealTimers();
	});

	describe("profile unload", () => {
		it("unloads a hidden profile once the timer elapses", () => {
			const hiddenId = createProfile();
			const tabs = fakeTabs();

			scheduler = new SleepScheduler({
				profiles: new Map([[hiddenId, { tabs, cache: fakeCache() }]]),
			});

			vi.advanceTimersByTime(THIRTY_ONE_MINUTES);

			expect(tabs.unloadAll).toHaveBeenCalledOnce();
		});

		it("never unloads the active profile", () => {
			const activeId = createProfile();
			const tabs = fakeTabs();
			tabStore.getState().setActiveTab("tab-1", activeId);

			scheduler = new SleepScheduler({
				profiles: new Map([[activeId, { tabs, cache: fakeCache() }]]),
			});

			vi.advanceTimersByTime(THIRTY_ONE_MINUTES);

			expect(tabs.unloadAll).not.toHaveBeenCalled();
		});

		it("skips Keep loaded profiles", () => {
			const keptId = createProfile({ keepLoaded: true });
			const tabs = fakeTabs();

			scheduler = new SleepScheduler({
				profiles: new Map([[keptId, { tabs, cache: fakeCache() }]]),
			});

			vi.advanceTimersByTime(THIRTY_ONE_MINUTES);

			expect(tabs.unloadAll).not.toHaveBeenCalled();
			expect(tabs.sleep).not.toHaveBeenCalled();
		});

		it("skips profiles holding a protected tab", () => {
			const hiddenId = createProfile();
			const tabs = fakeTabs();
			tabs.isProtected.mockReturnValue(true);

			scheduler = new SleepScheduler({
				profiles: new Map([[hiddenId, { tabs, cache: fakeCache() }]]),
			});

			vi.advanceTimersByTime(THIRTY_ONE_MINUTES);

			expect(tabs.unloadAll).not.toHaveBeenCalled();
		});

		it("skips already-unloaded profiles", () => {
			const hiddenId = createProfile();
			const tabs = fakeTabs([]);

			scheduler = new SleepScheduler({
				profiles: new Map([[hiddenId, { tabs, cache: fakeCache() }]]),
			});

			vi.advanceTimersByTime(THIRTY_ONE_MINUTES);

			expect(tabs.unloadAll).not.toHaveBeenCalled();
		});

		it("does nothing when the timer setting is off", () => {
			settingsStore.getState().update({ profileUnloadAfterMinutes: null });
			const hiddenId = createProfile();
			const tabs = fakeTabs();

			scheduler = new SleepScheduler({
				profiles: new Map([[hiddenId, { tabs, cache: fakeCache() }]]),
			});

			vi.advanceTimersByTime(THIRTY_ONE_MINUTES);

			expect(tabs.unloadAll).not.toHaveBeenCalled();
		});

		it("restarts the hidden clock when a profile is re-activated", () => {
			// Tab sleep stays off so the drained-by-tab-sleep path can't unload the
			// profile first — this test isolates the profile clock.
			settingsStore.getState().update({ tabSleepAfterMinutes: null });
			const id = createProfile();
			const tabs = fakeTabs();

			scheduler = new SleepScheduler({
				profiles: new Map([[id, { tabs, cache: fakeCache() }]]),
			});

			// 20 minutes hidden, then a visit resets the clock; 20 more minutes
			// after switching away must NOT reach the 30-minute deadline.
			vi.advanceTimersByTime(20 * 60_000);
			tabStore.getState().setActiveTab("tab-1", id);
			tabStore.getState().setActiveTab(null, null);
			vi.advanceTimersByTime(20 * 60_000);

			expect(tabs.unloadAll).not.toHaveBeenCalled();

			vi.advanceTimersByTime(11 * 60_000);

			expect(tabs.unloadAll).toHaveBeenCalledOnce();
		});
	});

	describe("tab sleep", () => {
		it("sleeps a background tab once the timer elapses", () => {
			const activeId = createProfile();
			const tabs = fakeTabs(["tab-1", "tab-2"]);

			scheduler = new SleepScheduler({
				profiles: new Map([[activeId, { tabs, cache: fakeCache() }]]),
			});

			// tab-1 goes to the background when tab-2 takes over.
			tabStore.getState().setActiveTab("tab-1", activeId);
			tabStore.getState().setActiveTab("tab-2", activeId);
			vi.advanceTimersByTime(SIXTEEN_MINUTES);

			expect(tabs.sleep).toHaveBeenCalledExactlyOnceWith("tab-1");
		});

		it("never sleeps the active tab", () => {
			const activeId = createProfile();
			const tabs = fakeTabs(["tab-1"]);

			scheduler = new SleepScheduler({
				profiles: new Map([[activeId, { tabs, cache: fakeCache() }]]),
			});

			tabStore.getState().setActiveTab("tab-1", activeId);
			vi.advanceTimersByTime(SIXTEEN_MINUTES);

			expect(tabs.sleep).not.toHaveBeenCalled();
		});

		it("skips protected background tabs", () => {
			const activeId = createProfile();
			const tabs = fakeTabs(["tab-1", "tab-2"]);
			tabs.isProtected.mockImplementation((tabId) => tabId === "tab-1");

			scheduler = new SleepScheduler({
				profiles: new Map([[activeId, { tabs, cache: fakeCache() }]]),
			});

			tabStore.getState().setActiveTab("tab-1", activeId);
			tabStore.getState().setActiveTab("tab-2", activeId);
			vi.advanceTimersByTime(SIXTEEN_MINUTES);

			expect(tabs.sleep).not.toHaveBeenCalled();
		});

		it("does nothing when the tab timer setting is off", () => {
			settingsStore.getState().update({ tabSleepAfterMinutes: null });
			const activeId = createProfile();
			const tabs = fakeTabs(["tab-1", "tab-2"]);

			scheduler = new SleepScheduler({
				profiles: new Map([[activeId, { tabs, cache: fakeCache() }]]),
			});

			tabStore.getState().setActiveTab("tab-1", activeId);
			tabStore.getState().setActiveTab("tab-2", activeId);
			vi.advanceTimersByTime(THIRTY_ONE_MINUTES);

			expect(tabs.sleep).not.toHaveBeenCalled();
		});

		it("restarts the background clock when a tab is re-activated", () => {
			const activeId = createProfile();
			const tabs = fakeTabs(["tab-1", "tab-2"]);

			scheduler = new SleepScheduler({
				profiles: new Map([[activeId, { tabs, cache: fakeCache() }]]),
			});

			tabStore.getState().setActiveTab("tab-1", activeId);
			tabStore.getState().setActiveTab("tab-2", activeId);
			vi.advanceTimersByTime(10 * 60_000);
			tabStore.getState().setActiveTab("tab-1", activeId);
			tabStore.getState().setActiveTab("tab-2", activeId);
			vi.advanceTimersByTime(10 * 60_000);

			expect(tabs.sleep).not.toHaveBeenCalled();

			vi.advanceTimersByTime(6 * 60_000);

			expect(tabs.sleep).toHaveBeenCalledExactlyOnceWith("tab-1");
		});
	});

	// The trim hangs off the unload, so every case advances past the 30-minute
	// unload deadline. Async advance, because the trim resolves on microtasks a
	// synchronous advance would never flush.
	describe("cache trim", () => {
		it("trims a profile that unloads over budget", async () => {
			const hiddenId = createProfile();
			const tabs = fakeTabs();
			const cache = fakeCache(400);

			scheduler = new SleepScheduler({
				profiles: new Map([[hiddenId, { tabs, cache }]]),
			});

			await vi.advanceTimersByTimeAsync(THIRTY_ONE_MINUTES);

			expect(cache.trim).toHaveBeenCalledOnce();
		});

		it("leaves a profile that unloads under budget", async () => {
			const hiddenId = createProfile();
			const tabs = fakeTabs();
			const cache = fakeCache(50);

			scheduler = new SleepScheduler({
				profiles: new Map([[hiddenId, { tabs, cache }]]),
			});

			await vi.advanceTimersByTimeAsync(THIRTY_ONE_MINUTES);

			expect(cache.trim).not.toHaveBeenCalled();
		});

		it("never measures the cache when the budget setting is off", async () => {
			settingsStore.getState().update({ cacheBudgetMB: null });
			const hiddenId = createProfile();
			const tabs = fakeTabs();
			const cache = fakeCache(4000);

			scheduler = new SleepScheduler({
				profiles: new Map([[hiddenId, { tabs, cache }]]),
			});

			await vi.advanceTimersByTimeAsync(THIRTY_ONE_MINUTES);

			expect(cache.size).not.toHaveBeenCalled();
			expect(cache.trim).not.toHaveBeenCalled();
		});

		it("never trims a Keep loaded profile, however large", async () => {
			const keptId = createProfile({ keepLoaded: true });
			const tabs = fakeTabs();
			const cache = fakeCache(4000);

			scheduler = new SleepScheduler({
				profiles: new Map([[keptId, { tabs, cache }]]),
			});

			await vi.advanceTimersByTimeAsync(THIRTY_ONE_MINUTES);

			expect(cache.trim).not.toHaveBeenCalled();
		});

		it("never trims on the tab-sleep path", async () => {
			settingsStore.getState().update({ profileUnloadAfterMinutes: null });
			const activeId = createProfile();
			const tabs = fakeTabs(["tab-1", "tab-2"]);
			const cache = fakeCache(4000);

			scheduler = new SleepScheduler({
				profiles: new Map([[activeId, { tabs, cache }]]),
			});

			tabStore.getState().setActiveTab("tab-1", activeId);
			tabStore.getState().setActiveTab("tab-2", activeId);
			await vi.advanceTimersByTimeAsync(THIRTY_ONE_MINUTES);

			expect(tabs.sleep).toHaveBeenCalled();
			expect(cache.trim).not.toHaveBeenCalled();
		});

		it("does not stack a second trim while one is in flight", async () => {
			const hiddenId = createProfile();
			const tabs = fakeTabs();
			const cache = fakeCache(400);

			// Never resolves: the first trim stays in flight across later ticks.
			cache.trim.mockReturnValue(new Promise<void>(() => {}));

			scheduler = new SleepScheduler({
				profiles: new Map([[hiddenId, { tabs, cache }]]),
			});

			await vi.advanceTimersByTimeAsync(THIRTY_ONE_MINUTES);

			// Re-hide so the unload branch is reachable again on a later tick.
			tabStore.getState().setActiveTab("tab-1", hiddenId);
			tabStore.getState().setActiveTab(null, null);
			tabs.loadedTabIds.mockReturnValue(["tab-1"]);
			await vi.advanceTimersByTimeAsync(THIRTY_ONE_MINUTES);

			expect(cache.trim).toHaveBeenCalledOnce();
		});

		it("keeps ticking after a trim rejects", async () => {
			const hiddenId = createProfile();
			const tabs = fakeTabs();
			const cache = fakeCache(400);
			cache.trim.mockRejectedValue(new Error("disk busy"));
			const warn = vi.spyOn(console, "warn").mockImplementation(() => {});

			scheduler = new SleepScheduler({
				profiles: new Map([[hiddenId, { tabs, cache }]]),
			});

			await vi.advanceTimersByTimeAsync(THIRTY_ONE_MINUTES);

			expect(warn).toHaveBeenCalled();

			// The in-flight guard released, so a later unload can trim again.
			tabStore.getState().setActiveTab("tab-1", hiddenId);
			tabStore.getState().setActiveTab(null, null);
			tabs.loadedTabIds.mockReturnValue(["tab-1"]);
			await vi.advanceTimersByTimeAsync(THIRTY_ONE_MINUTES);

			expect(cache.trim).toHaveBeenCalledTimes(2);

			warn.mockRestore();
		});
	});
});
