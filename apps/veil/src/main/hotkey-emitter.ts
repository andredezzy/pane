import { EventEmitter } from "node:events";

export enum HotkeyEvent {
	FOCUS_ADDRESS_BAR = "FOCUS_ADDRESS_BAR",
	TAB_SWITCHER_FORWARD = "TAB_SWITCHER_FORWARD",
	TAB_SWITCHER_BACKWARD = "TAB_SWITCHER_BACKWARD",
}

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
