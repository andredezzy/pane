import { createStore } from "zustand/vanilla";

import { sync } from "./middlewares/sync";

export enum UpdateStatus {
	IDLE = "IDLE",
	CHECKING = "CHECKING",
	AVAILABLE = "AVAILABLE",
	DOWNLOADING = "DOWNLOADING",
}

export interface AvailableUpdate {
	version: string;
	dmgUrl: string;
	publishedAt: string;
}

export interface UpdateState {
	status: UpdateStatus;
	available: AvailableUpdate | null;
	lastCheckedAt: string | null;

	startChecking: () => void;
	finishChecking: (result: {
		available: AvailableUpdate | null;
		checkedAt: string;
	}) => void;
	checkFailed: () => void;
	startDownloading: () => void;
	finishDownloading: () => void;
}

// Update status is entirely main-process-owned (the main process polls GitHub
// and drives the download), the renderer only displays it and triggers checks
// through the `updates` tRPC router — so nothing it could push back should
// ever be applied. Not persisted: a fresh check runs on every launch anyway.
export const updateStore = createStore<UpdateState>()(
	sync(
		(set) => ({
			status: UpdateStatus.IDLE,
			available: null,
			lastCheckedAt: null,

			startChecking: () => set({ status: UpdateStatus.CHECKING }),

			finishChecking: ({ available, checkedAt }) =>
				set({
					status: available ? UpdateStatus.AVAILABLE : UpdateStatus.IDLE,
					available,
					lastCheckedAt: checkedAt,
				}),

			checkFailed: () => set({ status: UpdateStatus.IDLE }),

			startDownloading: () => set({ status: UpdateStatus.DOWNLOADING }),

			finishDownloading: () => set({ status: UpdateStatus.AVAILABLE }),
		}),
		{ name: "update-store", pushPartialize: () => ({}) },
	),
);
