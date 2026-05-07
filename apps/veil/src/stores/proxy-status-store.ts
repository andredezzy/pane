import { createStore } from "zustand/vanilla";

export enum ProxyStatus {
	TESTING = "TESTING",
	CONNECTED = "CONNECTED",
	FAILED = "FAILED",
}

interface ProxyStatusState {
	statuses: Record<string, ProxyStatus>;

	set: (profileId: string, status: ProxyStatus) => void;
	remove: (profileId: string) => void;
}

export const proxyStatusStore = createStore<ProxyStatusState>((set) => ({
	statuses: {},

	set: (profileId, status) => {
		set((state) => ({
			statuses: { ...state.statuses, [profileId]: status },
		}));
	},

	remove: (profileId) => {
		set((state) => {
			const { [profileId]: _, ...rest } = state.statuses;
			return { statuses: rest };
		});
	},
}));
