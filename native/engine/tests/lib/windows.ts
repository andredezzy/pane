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

// The windows once their count reaches `expected`, or the last list seen
// after `timeoutMs`: a window can be on screen briefly while another opens.
export async function settledWindowsOf(
	pid: number,
	expected: number,
	timeoutMs = 5_000,
): Promise<WindowList> {
	const deadline = Date.now() + timeoutMs;
	let list = await windowsOf(pid);

	while (list.count !== expected && Date.now() < deadline) {
		await Bun.sleep(250);
		list = await windowsOf(pid);
	}

	return list;
}

const DETAIL_SOURCE = join(
	import.meta.dir,
	"..",
	"tools",
	"image-detail.swift",
);

const DETAIL_BINARY = join(tmpdir(), "pane-tests", "image-detail");

// How much detail a region of a PNG has (fractions of its size): the
// luminance's standard deviation, 0 for a flat color.
export async function imageDetail(
	file: string,
	region: [number, number, number, number],
): Promise<{ mean: number; deviation: number }> {
	if (!existsSync(DETAIL_BINARY)) {
		mkdirSync(join(tmpdir(), "pane-tests"), { recursive: true });
		await Bun.$`swiftc -O ${DETAIL_SOURCE} -o ${DETAIL_BINARY}`.quiet();
	}

	return JSON.parse(
		await Bun.$`${DETAIL_BINARY} ${file} ${region.map(String)}`.text(),
	);
}
