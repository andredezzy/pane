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
				profiles: new Map([[hiddenId, { tabs }]]),
			});

			vi.advanceTimersByTime(THIRTY_ONE_MINUTES);

			expect(tabs.unloadAll).toHaveBeenCalledOnce();
		});

		it("never unloads the active profile", () => {
			const activeId = createProfile();
			const tabs = fakeTabs();
			tabStore.getState().setActiveTab("tab-1", activeId);

			scheduler = new SleepScheduler({
				profiles: new Map([[activeId, { tabs }]]),
			});

			vi.advanceTimersByTime(THIRTY_ONE_MINUTES);

			expect(tabs.unloadAll).not.toHaveBeenCalled();
		});

		it("skips Keep loaded profiles", () => {
			const keptId = createProfile({ keepLoaded: true });
			const tabs = fakeTabs();

			scheduler = new SleepScheduler({
				profiles: new Map([[keptId, { tabs }]]),
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
				profiles: new Map([[hiddenId, { tabs }]]),
			});

			vi.advanceTimersByTime(THIRTY_ONE_MINUTES);

			expect(tabs.unloadAll).not.toHaveBeenCalled();
		});

		it("skips already-unloaded profiles", () => {
			const hiddenId = createProfile();
			const tabs = fakeTabs([]);

			scheduler = new SleepScheduler({
				profiles: new Map([[hiddenId, { tabs }]]),
			});

			vi.advanceTimersByTime(THIRTY_ONE_MINUTES);

			expect(tabs.unloadAll).not.toHaveBeenCalled();
		});

		it("does nothing when the timer setting is off", () => {
			settingsStore.getState().update({ profileUnloadAfterMinutes: null });
			const hiddenId = createProfile();
			const tabs = fakeTabs();

			scheduler = new SleepScheduler({
				profiles: new Map([[hiddenId, { tabs }]]),
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
			scheduler = new SleepScheduler({ profiles: new Map([[id, { tabs }]]) });

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
				profiles: new Map([[activeId, { tabs }]]),
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
				profiles: new Map([[activeId, { tabs }]]),
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
				profiles: new Map([[activeId, { tabs }]]),
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
				profiles: new Map([[activeId, { tabs }]]),
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
				profiles: new Map([[activeId, { tabs }]]),
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
});
