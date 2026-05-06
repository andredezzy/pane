import type { StateCreator } from "zustand/vanilla";

import { serializeState } from "./serialize";

export type StoreName =
	| "profile-store"
	| "tab-store"
	| "navigation-store"
	| "settings-store"
	| "extension-store"
	| "security-store";

function isRenderer(): boolean {
	return typeof window !== "undefined";
}

export function sync<TState>(
	storeCreator: StateCreator<TState, [], []>,
	config: { name: StoreName },
): StateCreator<TState, [], []> {
	const { name } = config;

	return (set, get, api) => {
		if (!isRenderer()) {
			return storeCreator(set, get, api);
		}

		let applyingRemote = false;

		let trpc: Awaited<typeof import("../../renderer/trpc")>["trpc"] | null =
			null;

		import("../../renderer/trpc").then((mod) => {
			trpc = mod.trpc;

			trpc?.stores.sync.subscribe(
				{ name },
				{
					onData(serialized: string) {
						applyingRemote = true;
						set((state) => ({ ...state, ...JSON.parse(serialized) }));
						applyingRemote = false;
					},
					onError(err) {
						console.error(`[sync] subscription error for ${name}:`, err);
					},
				},
			);
		});

		const syncedSet: typeof set = (updater, replace) => {
			set(updater, replace as never);

			if (applyingRemote || !trpc) {
				return;
			}

			trpc.stores.push
				.mutate({ name, state: serializeState(get()) })
				.catch((err) => {
					console.error(`[sync] push error for ${name}:`, err);
				});
		};

		return storeCreator(syncedSet, get, api);
	};
}
