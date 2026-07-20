import { createJSONStorage, persist } from "zustand/middleware";
import { createStore } from "zustand/vanilla";

import { fsStorage } from "./middlewares/fs-storage";
import { sync } from "./middlewares/sync";

// String union, not an enum: the values feed Electron's nativeTheme.themeSource,
// whose exact strings are the contract.
export type Theme = "system" | "light" | "dark";

export interface AppSettings {
	chromiumPath: string;
	theme: Theme;
}

export interface SettingsState {
	settings: AppSettings;

	update: (settings: Partial<AppSettings>) => void;
}

export const settingsStore = createStore<SettingsState>()(
	persist(
		sync(
			(set) => ({
				settings: { chromiumPath: "", theme: "system" },

				update: (settings) =>
					set((state) => ({ settings: { ...state.settings, ...settings } })),
			}),
			{ name: "settings-store" },
		),
		{
			name: "settings",
			storage: createJSONStorage(() => fsStorage),
			skipHydration: true,
			partialize: (state) => ({ settings: state.settings }),
			// Merge the persisted settings INTO the defaults instead of replacing the
			// object wholesale — a settings.json written before a field existed must
			// not wipe that field's default.
			merge: (persisted, current) => ({
				...current,
				settings: {
					...current.settings,
					...(persisted as Partial<SettingsState> | undefined)?.settings,
				},
			}),
		},
	),
);
