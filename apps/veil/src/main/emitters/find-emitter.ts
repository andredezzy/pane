import { EventEmitter } from "node:events";

import type { FindResult } from "../../constants/find-result";

const CHANNEL = "find-result";

export class FindEmitter extends EventEmitter {
	constructor() {
		super();
		this.setMaxListeners(100);
	}

	emitResult(result: FindResult): void {
		this.emit(CHANNEL, result);
	}

	onResult(callback: (result: FindResult) => void): void {
		this.on(CHANNEL, callback);
	}

	offResult(callback: (result: FindResult) => void): void {
		this.off(CHANNEL, callback);
	}
}
