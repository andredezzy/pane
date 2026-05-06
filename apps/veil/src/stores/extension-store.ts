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
				set((s) => ({
					extensions: {
						...s.extensions,
						[profileId]: [...(s.extensions[profileId] ?? []), ext],
					},
				})),

			removeExtension: (profileId, extensionId) =>
				set((s) => ({
					extensions: {
						...s.extensions,
						[profileId]: (s.extensions[profileId] ?? []).filter(
							(e) => e.id !== extensionId,
						),
					},
				})),

			clearProfile: (profileId) =>
				set((s) => {
					const { [profileId]: _, ...rest } = s.extensions;

					return { extensions: rest };
				}),
		}),
		{ name: "extension-store" },
	),
);
