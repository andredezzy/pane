import { createStore } from "zustand/vanilla";

import { sync } from "./middlewares/sync";

export interface ExtensionInfo {
	id: string;
	name: string;
	version: string;
}

interface ExtensionState {
	extensions: Record<string, ExtensionInfo[]>;

	addExtension: (profileId: string, ext: ExtensionInfo) => void;
	removeExtension: (profileId: string, extensionId: string) => void;
	clearProfile: (profileId: string) => void;
}

export const extensionStore = createStore<ExtensionState>()(
	sync(
		(set) => ({
			extensions: {},

			addExtension: (profileId, ext) =>
				set((state) => ({
					extensions: {
						...state.extensions,
						[profileId]: [...(state.extensions[profileId] ?? []), ext],
					},
				})),

			removeExtension: (profileId, extensionId) =>
				set((state) => ({
					extensions: {
						...state.extensions,
						[profileId]: (state.extensions[profileId] ?? []).filter(
							(extension) => extension.id !== extensionId,
						),
					},
				})),

			clearProfile: (profileId) =>
				set((state) => {
					const { [profileId]: _, ...rest } = state.extensions;

					return { extensions: rest };
				}),
		}),
		{ name: "extension-store" },
	),
);
