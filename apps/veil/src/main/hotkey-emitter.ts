import { EventEmitter } from "node:events";

import { HotkeyEvent } from "../constants/hotkey-event";

export { HotkeyEvent };

const CHANNEL = "hotkey";

export class HotkeyEmitter extends EventEmitter {
	emitHotkey(event: HotkeyEvent): void {
		this.emit(CHANNEL, event);
	}

	onHotkey(callback: (event: HotkeyEvent) => void): void {
		this.on(CHANNEL, callback);
	}

	offHotkey(callback: (event: HotkeyEvent) => void): void {
		this.off(CHANNEL, callback);
	}
}
