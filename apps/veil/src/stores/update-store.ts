import { createStore } from "zustand/vanilla";

import { sync } from "./middlewares/sync";

export enum UpdateStatus {
	IDLE = "IDLE",
	CHECKING = "CHECKING",
	AVAILABLE = "AVAILABLE",
	DOWNLOADING = "DOWNLOADING",
	DOWNLOADED = "DOWNLOADED",
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
	/** 0–1 while a download runs, null otherwise. */
	downloadProgress: number | null;
	/** Whole seconds left at the current rate, null until the rate settles. */
	downloadEtaSeconds: number | null;

	startChecking: () => void;
	finishChecking: (result: {
		available: AvailableUpdate | null;
		checkedAt: string;
	}) => void;
	checkFailed: () => void;
	startDownloading: () => void;
	setDownloadProgress: (update: {
		progress: number;
		etaSeconds: number | null;
	}) => void;
	finishDownloading: () => void;
	downloadFailed: () => void;
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
			downloadProgress: null,
			downloadEtaSeconds: null,

			startChecking: () => set({ status: UpdateStatus.CHECKING }),

			finishChecking: ({ available, checkedAt }) =>
				set({
					status: available ? UpdateStatus.AVAILABLE : UpdateStatus.IDLE,
					available,
					lastCheckedAt: checkedAt,
				}),

			checkFailed: () => set({ status: UpdateStatus.IDLE }),

			startDownloading: () =>
				set({
					status: UpdateStatus.DOWNLOADING,
					downloadProgress: 0,
					downloadEtaSeconds: null,
				}),

			setDownloadProgress: ({ progress, etaSeconds }) =>
				set({ downloadProgress: progress, downloadEtaSeconds: etaSeconds }),

			finishDownloading: () =>
				set({
					status: UpdateStatus.DOWNLOADED,
					downloadProgress: null,
					downloadEtaSeconds: null,
				}),

			downloadFailed: () =>
				set({
					status: UpdateStatus.AVAILABLE,
					downloadProgress: null,
					downloadEtaSeconds: null,
				}),
		}),
		{ name: "update-store", pushPartialize: () => ({}) },
	),
);
