import { createStore } from "zustand/vanilla";

import { sync } from "./middlewares/sync";

export enum Page {
	BROWSER = "BROWSER",
	SETTINGS = "SETTINGS",
}

interface NavigationState {
	page: Page;

	navigate: (page: Page) => void;
}

export const navigationStore = createStore<NavigationState>()(
	sync(
		(set) => ({
			page: Page.BROWSER,

			navigate: (page) => set({ page }),
		}),
		{ name: "navigation-store" },
	),
);
