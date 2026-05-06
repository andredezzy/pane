import { initTRPC } from "@trpc/server";
import type { BrowserWindow } from "electron";
import type { StoreApi } from "zustand/vanilla";

import type { StoreName } from "../../stores/middlewares/sync";
import type { HotkeyEmitter } from "../hotkey-emitter";
import type { Pane } from "../pane";

export type { StoreName };

export interface Context {
	pane: Pane;
	stores: Record<StoreName, StoreApi<object>>;
	surface: BrowserWindow;
	hotkeyEmitter: HotkeyEmitter;
}

const t = initTRPC.context<Context>().create({ isServer: true });

export const router = t.router;
export const procedure = t.procedure;
