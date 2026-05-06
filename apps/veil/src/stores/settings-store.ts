import { createJSONStorage, persist } from "zustand/middleware";
import { createStore } from "zustand/vanilla";

import { fsStorage } from "./middlewares/fs-storage";
import { sync } from "./middlewares/sync";

export interface AppSettings {
	chromiumPath: string;
}

export interface SettingsState {
	settings: AppSettings;

	update: (settings: AppSettings) => void;
}

export const settingsStore = createStore<SettingsState>()(
	persist(
		sync(
			(set) => ({
				settings: { chromiumPath: "" },

				update: (settings) => set({ settings }),
			}),
			{ name: "settings-store" },
		),
		{
			name: "settings",
			storage: createJSONStorage(() => fsStorage),
			skipHydration: true,
			partialize: (state) => ({ settings: state.settings }),
		},
	),
);
