import { pollValue } from "./poll";
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

type Bounds = { X: number; Y: number; Width: number; Height: number };

export type MenuWindow = {
	bounds: Bounds;
	// The screen the menu's top left is on: all of it, and what the menu bar
	// and the Dock leave.
	screen: { frame: Bounds; visible: Bounds };
};

// The process's open menus, the system's (NSMenu), on screen: their windows'
// bounds, in points from the main display's top left, as the window list
// writes them.
export async function menusOf(pid: number): Promise<MenuWindow[]> {
	const binary = await swiftTool("windows");

	return (
		JSON.parse(await Bun.$`${binary} --menus ${String(pid)}`.text()) as {
			windows: MenuWindow[];
		}
	).windows;
}

// The windows once their count reaches `expected`, or the last list seen
// after `timeoutMs`: a window can be on screen briefly while another opens.
export async function settledWindowsOf(
	pid: number,
	expected: number,
	timeoutMs = 5_000,
): Promise<WindowList> {
	return pollValue(
		() => windowsOf(pid),
		(list) => list.count === expected,
		timeoutMs,
		250,
	);
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
