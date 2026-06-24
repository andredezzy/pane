import { createStore } from "zustand/vanilla";

// Renderer-only UI state: which profiles are expanded (open) in the sidebar.
// Shared so the Ctrl+Tab switcher can show tabs only from open profiles. Not
// synced to main and not persisted — collapsed-by-default on every launch.
interface SidebarState {
	expandedProfileIds: string[];

	toggleProfile: (profileId: string) => void;
}

export const sidebarStore = createStore<SidebarState>()((set) => ({
	expandedProfileIds: [],

	toggleProfile: (profileId) =>
		set((state) => ({
			expandedProfileIds: state.expandedProfileIds.includes(profileId)
				? state.expandedProfileIds.filter((id) => id !== profileId)
				: [...state.expandedProfileIds, profileId],
		})),
}));
