import { registerSyncTransport } from "../stores/middlewares/sync";
import { trpc } from "./trpc";

// Wire the cross-process store sync to tRPC over IPC. This is renderer-only, so
// the shared sync middleware never imports renderer code — the main process
// simply never registers a transport and keeps its stores authoritative.
registerSyncTransport({
	push: (input) => trpc.stores.push.mutate(input),
	subscribe: (input, handlers) => {
		trpc.stores.sync.subscribe(input, handlers);
	},
});
