import { existsSync, mkdirSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const SOURCE = join(import.meta.dir, "..", "tools", "windows.swift");
const BINARY = join(tmpdir(), "pane-tests", "windows");

export type WindowList = {
	count: number;
	windows: {
		pid: number;
		owner: string;
		title: string;
		bounds: Record<string, number>;
	}[];
};

// On-screen, normal-layer windows owned by the given processes.
export async function windowsOf(...pids: number[]): Promise<WindowList> {
	if (!existsSync(BINARY)) {
		mkdirSync(join(tmpdir(), "pane-tests"), { recursive: true });
		await Bun.$`swiftc -O ${SOURCE} -o ${BINARY}`.quiet();
	}

	return JSON.parse(await Bun.$`${BINARY} ${pids.map(String)}`.text());
}
