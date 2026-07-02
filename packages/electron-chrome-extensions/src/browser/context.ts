import { EventEmitter } from "node:events";
import { ExtensionRouter } from "./router";
import { ExtensionStore } from "./store";

/** Shared context for extensions in a session. */
export interface ExtensionContext {
	emit: (typeof EventEmitter)["prototype"]["emit"];
	on: (typeof EventEmitter)["prototype"]["on"];
	router: ExtensionRouter;
	session: Electron.Session;
	store: ExtensionStore;
}
