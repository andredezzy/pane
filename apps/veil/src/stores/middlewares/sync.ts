import type { StateCreator } from "zustand/vanilla";

import { serializeState } from "./serialize";

export type StoreName =
	| "profile-store"
	| "tab-store"
	| "navigation-store"
	| "settings-store"
	| "extension-store"
	| "security-store"
	| "sidebar-store";

export interface SyncTransportHandlers {
	onData: (serialized: string) => void;
	onError: (error: unknown) => void;
}

/**
 * Carries store changes between processes. The renderer registers a transport
 * backed by tRPC over IPC (see `renderer/sync-transport.ts`); the main process
 * never registers one, so its stores stay local and authoritative.
 */
export interface SyncTransport {
	push: (input: { name: StoreName; state: string }) => Promise<unknown>;
	subscribe: (
		input: { name: StoreName },
		handlers: SyncTransportHandlers,
	) => void;
}

let transport: SyncTransport | null = null;
const pendingWires: Array<(transport: SyncTransport) => void> = [];

export function registerSyncTransport(syncTransport: SyncTransport): void {
	transport = syncTransport;

	for (const wire of pendingWires.splice(0)) {
		wire(syncTransport);
	}
}

// Late-binding so store creation never has to race transport registration: a
// store made before the renderer boots is wired the moment it registers; one
// made after wires immediately. In the main process nothing registers, so the
// wire never runs and the store stays local.
function whenTransportReady(wire: (transport: SyncTransport) => void): void {
	if (transport) {
		wire(transport);
	} else {
		pendingWires.push(wire);
	}
}

export interface SyncConfig<TState> {
	name: StoreName;
	/**
	 * Restricts which fields the renderer is allowed to push back to the main
	 * process. Mirrors zustand persist's `partialize`. Omit any field that is
	 * authoritatively owned by the main process — otherwise a stale renderer
	 * snapshot can clobber a value main wrote moments earlier (e.g. a security
	 * counter the backend just reset). Defaults to pushing the full state.
	 */
	pushPartialize?: (state: TState) => Partial<TState>;
}

export function sync<TState>(
	storeCreator: StateCreator<TState, [], []>,
	config: SyncConfig<TState>,
): StateCreator<TState, [], []> {
	const { name, pushPartialize } = config;

	return (set, get, api) => {
		let applyingRemote = false;

		whenTransportReady((syncTransport) => {
			syncTransport.subscribe(
				{ name },
				{
					onData(serialized) {
						applyingRemote = true;
						set((state) => ({ ...state, ...JSON.parse(serialized) }));
						applyingRemote = false;
					},
					onError(error) {
						console.error(`[sync] subscription error for ${name}:`, error);
					},
				},
			);
		});

		const syncedSet: typeof set = (updater, replace) => {
			set(updater, replace as never);

			if (applyingRemote || !transport) {
				return;
			}

			const snapshot = pushPartialize ? pushPartialize(get()) : get();

			transport
				.push({ name, state: serializeState(snapshot) })
				.catch((error) => {
					console.error(`[sync] push error for ${name}:`, error);
				});
		};

		return storeCreator(syncedSet, get, api);
	};
}
