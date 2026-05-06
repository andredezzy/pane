import { createStore } from "zustand/vanilla";

import { sync } from "./middlewares/sync";

const MAX_CLOSED_TABS = 20;

export interface ClosedTab {
	url: string;
	profileId: string;
	title?: string;
	favicon?: string;
}

export interface TabState {
	activeTabId: string | null;
	activeProfileId: string | null;
	loadingTabIds: string[];
	mruHistory: string[];
	closedTabs: ClosedTab[];

	setActiveTab: (tabId: string | null, profileId: string | null) => void;
	setLoading: (tabId: string, isLoading: boolean) => void;
	pushMru: (tabId: string) => void;
	removeMru: (tabId: string) => void;
	pushClosedTab: (tab: ClosedTab) => void;
	popClosedTab: () => ClosedTab | undefined;
}

export const tabStore = createStore<TabState>()(
	sync(
		(set, get) => ({
			activeTabId: null,
			activeProfileId: null,
			loadingTabIds: [],
			mruHistory: [],
			closedTabs: [],

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

			pushMru: (tabId) =>
				set((state) => ({
					mruHistory: [tabId, ...state.mruHistory.filter((id) => id !== tabId)],
				})),

			removeMru: (tabId) =>
				set((state) => ({
					mruHistory: state.mruHistory.filter((id) => id !== tabId),
				})),

			pushClosedTab: (tab) =>
				set((state) => ({
					closedTabs: [tab, ...state.closedTabs].slice(0, MAX_CLOSED_TABS),
				})),

			popClosedTab: () => {
				const { closedTabs } = get();

				if (closedTabs.length === 0) {
					return undefined;
				}

				const [first, ...rest] = closedTabs;

				set({ closedTabs: rest });

				return first;
			},
		}),
		{ name: "tab-store" },
	),
);
