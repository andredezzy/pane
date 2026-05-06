import { createJSONStorage, persist } from "zustand/middleware";
import { createStore } from "zustand/vanilla";

import { flushKey, fsStorage } from "./middlewares/fs-storage";
import { sync } from "./middlewares/sync";

export interface Pin {
	hash: string;
	length: number;
}

export const MAX_ATTEMPTS = 5;

export enum PinScreenMode {
	SETUP = "SETUP",
	CHANGE = "CHANGE",
	REMOVE = "REMOVE",
}

export interface SecurityState {
	pin: Pin | null;
	failedAttempts: number;
	isLocked: boolean;
	pinScreenMode: PinScreenMode | null;

	setPin: (pin: Pin) => void;
	clearPin: () => void;
	lock: () => void;
	unlock: () => void;
	recordFailedAttempt: () => number;
	resetAttempts: () => void;
	showPinScreen: (mode: PinScreenMode) => void;
	dismissPinScreen: () => void;
}

export const securityStore = createStore<SecurityState>()(
	persist(
		sync(
			(set, get) => ({
				pin: null,
				failedAttempts: 0,
				isLocked: false,
				pinScreenMode: null,

				setPin: (pin) => set({ pin }),
				clearPin: () => set({ pin: null, isLocked: false, failedAttempts: 0 }),
				lock: () => set({ isLocked: true }),
				unlock: () => set({ isLocked: false }),

				recordFailedAttempt: () => {
					const next = get().failedAttempts + 1;
					set({ failedAttempts: next });
					flushKey("security");

					return next;
				},

				resetAttempts: () => set({ failedAttempts: 0 }),
				showPinScreen: (mode) => set({ pinScreenMode: mode }),
				dismissPinScreen: () => set({ pinScreenMode: null }),
			}),
			{ name: "security-store" },
		),
		{
			name: "security",
			storage: createJSONStorage(() => fsStorage),
			skipHydration: true,
			partialize: (state) => ({
				pin: state.pin,
				failedAttempts: state.failedAttempts,
			}),
			merge: (persisted, current) => ({
				...current,
				...(persisted as Partial<SecurityState>),
			}),
		},
	),
);
