import { swiftTool } from "./tools";

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
	const binary = await swiftTool("windows");

	return JSON.parse(await Bun.$`${binary} ${pids.map(String)}`.text());
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

// How much detail a region of a PNG has (fractions of its size): the
// luminance's standard deviation, 0 for a flat color.
export async function imageDetail(
	file: string,
	region: [number, number, number, number],
): Promise<{ mean: number; deviation: number }> {
	const binary = await swiftTool("image-detail");

	return JSON.parse(
		await Bun.$`${binary} ${file} ${region.map(String)}`.text(),
	);
}
