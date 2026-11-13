import { bySystem, noWindowsSide } from "./platform";
import { pollValue } from "./poll";
import { swiftTool, windowsDesktop } from "./tools";

export type WindowList = {
	count: number;
	windows: {
		pid: number;
		owner: string;
		title: string;
		bounds: Record<string, number>;
	}[];
};

// On-screen, normal-layer windows owned by the given processes: on Windows,
// the visible top-level windows that are no tool window (a menu, a tooltip),
// neither minimized nor cloaked, their bounds in the screen's pixels.
export const windowsOf: (...pids: number[]) => Promise<WindowList> = bySystem({
	mac: async (...pids: number[]): Promise<WindowList> => {
		const binary = await swiftTool("windows");

		return JSON.parse(await Bun.$`${binary} ${pids.map(String)}`.text());
	},
	windows: async (...pids: number[]): Promise<WindowList> => {
		const windows = JSON.parse(
			(
				await windowsDesktop(
					"[Console]::Write((ConvertTo-Json -Compress -Depth 4 -InputObject @([PaneDesktop]::WindowsOf([uint32[]]($env:PANE_PIDS -split ',')))))",
					{ PANE_PIDS: pids.join(",") },
				)
			).trim() || "[]",
		) as WindowList["windows"];

		return { count: windows.length, windows };
	},
});

// An element of a window's accessibility tree, as a screen reader reaches it.
export type AccessibleElement = {
	depth: number;
	role: string;
	subrole: string;
	title: string;
	description: string;
	value: string;
};

// What a screen reader reaches of the process's windows, element by element
// in VoiceOver's order. It reads only; a locked screen hides every window.
// Windows' is UI Automation's, which W8 checks with Narrator (ADR 0071).
export async function accessibilityTree(
	pid: number,
): Promise<AccessibleElement[]> {
	const binary = await bySystem({
		mac: () => swiftTool("ax-tree"),
		windows: async (): Promise<string> => {
			throw noWindowsSide(
				"the accessibility tree",
				"W8",
				"Windows' is UI Automation's, checked with Narrator (ADR 0071)",
			);
		},
	})();

	return JSON.parse(await Bun.$`${binary} ${String(pid)}`.text());
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
// writes them. Windows' menus are Views' own (MenuHost), each where its
// trigger's AnchoredMenu put it: a check of the front, as the Mac's.
export async function menusOf(pid: number): Promise<MenuWindow[]> {
	const binary = await bySystem({
		mac: () => swiftTool("windows"),
		windows: async (): Promise<string> => {
			throw noWindowsSide(
				"the system's menus on screen",
				"W5",
				"where Windows' menus show is drawn with the rest of its look",
			);
		},
	})();

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
// luminance's standard deviation, 0 for a flat color. Windows reaches it
// only behind the UI driven (canDriveUI), which is W8's there.
export async function imageDetail(
	file: string,
	region: [number, number, number, number],
): Promise<{ mean: number; deviation: number }> {
	const binary = await bySystem({
		mac: () => swiftTool("image-detail"),
		windows: async (): Promise<string> => {
			throw noWindowsSide(
				"an image's detail",
				"W8",
				"only a check of the UI driven reads it",
			);
		},
	})();

	return JSON.parse(
		await Bun.$`${binary} ${file} ${region.map(String)}`.text(),
	);
}
