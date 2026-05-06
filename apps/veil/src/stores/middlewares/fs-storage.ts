import type { StateStorage } from "zustand/middleware";

const isMain = typeof window === "undefined";

type Fs = typeof import("node:fs");
type Path = typeof import("node:path");

let _fs: Fs | undefined;
let _path: Path | undefined;
let _dataDir: string | undefined;

function getFs(): Fs {
	if (!_fs) {
		_fs = require("node:fs");
	}

	return _fs as Fs;
}

function resolvePath(name: string): string {
	if (!_path) {
		_path = require("node:path");
	}

	if (!_dataDir) {
		_dataDir = require("electron").app.getPath("userData");
	}

	return (_path as Path).join(_dataDir as string, `${name}.json`);
}

const pendingWrites = new Map<
	string,
	{ timer: ReturnType<typeof setTimeout>; value: string }
>();

const DEBOUNCE_MS = 300;

function writeToDisk(name: string, value: string): void {
	const filePath = resolvePath(name);
	const tmpPath = `${filePath}.tmp`;
	const nodeFs = getFs();
	nodeFs.writeFileSync(tmpPath, value, "utf-8");
	nodeFs.renameSync(tmpPath, filePath);
}

export const fsStorage: StateStorage = {
	getItem(name: string): string | null {
		if (!isMain) {
			return null;
		}

		try {
			return getFs().readFileSync(resolvePath(name), "utf-8");
		} catch {
			return null;
		}
	},

	setItem(name: string, value: string): void {
		if (!isMain) {
			return;
		}

		const existing = pendingWrites.get(name);

		if (existing) {
			clearTimeout(existing.timer);
		}

		const timer = setTimeout(() => {
			pendingWrites.delete(name);
			writeToDisk(name, value);
		}, DEBOUNCE_MS);

		pendingWrites.set(name, { timer, value });
	},

	removeItem(name: string): void {
		if (!isMain) {
			return;
		}

		try {
			getFs().unlinkSync(resolvePath(name));
		} catch {}
	},
};

export function flushKey(name: string): void {
	if (!isMain) {
		return;
	}

	const pending = pendingWrites.get(name);

	if (!pending) {
		return;
	}

	clearTimeout(pending.timer);
	pendingWrites.delete(name);
	writeToDisk(name, pending.value);
}

export function flushPendingWrites(): void {
	for (const [name, { timer, value }] of pendingWrites) {
		clearTimeout(timer);
		writeToDisk(name, value);
	}
	pendingWrites.clear();
}
