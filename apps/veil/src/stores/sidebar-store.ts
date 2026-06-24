import { createStore } from "zustand/vanilla";

import { sync } from "./middlewares/sync";

interface SidebarState {
	expandedProfileIds: string[];

	toggleProfile: (profileId: string) => void;
}

// Which profiles are expanded (open) in the sidebar. Synced across processes so
// the Ctrl+Tab switcher — which renders in the separate surface window, a
// different renderer than the sidebar — can show tabs only from open profiles.
// Not persisted: profiles are collapsed-by-default on every launch.
export const sidebarStore = createStore<SidebarState>()(
	sync(
		(set) => ({
			expandedProfileIds: [],

			toggleProfile: (profileId) =>
				set((state) => ({
					expandedProfileIds: state.expandedProfileIds.includes(profileId)
						? state.expandedProfileIds.filter((id) => id !== profileId)
						: [...state.expandedProfileIds, profileId],
				})),
		}),
		{ name: "sidebar-store" },
	),
);
