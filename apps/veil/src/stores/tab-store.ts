import { createStore } from "zustand/vanilla";

import { sync } from "./middlewares/sync";

export interface TabState {
	activeTabId: string | null;
	activeProfileId: string | null;
	loadingTabIds: string[];

	setActiveTab: (tabId: string | null, profileId: string | null) => void;
	setLoading: (tabId: string, isLoading: boolean) => void;
}

export const tabStore = createStore<TabState>()(
	sync(
		(set) => ({
			activeTabId: null,
			activeProfileId: null,
			loadingTabIds: [],

			setActiveTab: (tabId, profileId) =>
				set({ activeTabId: tabId, activeProfileId: profileId }),

			setLoading: (tabId, isLoading) =>
				set((state) => {
					if (isLoading) {
						if (state.loadingTabIds.includes(tabId)) {
							return state;
						}

						return { loadingTabIds: [...state.loadingTabIds, tabId] };
					}

					return {
						loadingTabIds: state.loadingTabIds.filter((id) => id !== tabId),
					};
				}),
		}),
		{ name: "tab-store" },
	),
);
