import { bySystem, noWindowsSide } from "./platform";
import { pollValue } from "./poll";
import { swiftTool, windowsDesktop } from "./tools";
import { type UiaElement, uiaTree, uiaWindows } from "./uia";

export type WindowList = {
	count: number;
	windows: {
		// The window's number: its CGWindowID on the Mac, its HWND on Windows.
		id: number;
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

// Every window the processes own, at any layer, on screen or not: what the
// computer-use driver holds of an app is proved a test copy's against it.
export const ownedWindowIds = bySystem({
	mac: async (...pids: number[]): Promise<number[]> => {
		const binary = await swiftTool("windows");

		const list = JSON.parse(
			await Bun.$`${binary} --owned ${pids.map(String)}`.text(),
		) as WindowList;

		return list.windows.map((window) => window.id);
	},
	windows: async (): Promise<number[]> => {
		throw noWindowsSide(
			"the windows a computer-use driver holds",
			"Windows' UI is driven through UI Automation, checked with Narrator, ADR 0071",
		);
	},
});

// An element of a window's accessibility tree, as a screen reader reaches it.
// The icons Chrome's tray holds in the system's notification area for the
// app's process, by id: Windows' alone (status_tray_win.cc), where an
// extension that asks the "background" permission put Chrome's background
// mode's (ADR 0010). Chrome makes no status item on the Mac
// (background_mode_manager.cc, CreateStatusTrayIcon).
export const trayIconsOf = bySystem({
	mac: async (_pid: number): Promise<number[]> => [],
	windows: async (pid: number): Promise<number[]> =>
		JSON.parse(
			(
				await windowsDesktop(
					"[Console]::Write((ConvertTo-Json -Compress -InputObject @([PaneDesktop]::TrayIconsOf([uint32]$env:PANE_PID))))",
					{ PANE_PID: String(pid) },
				)
			).trim() || "[]",
		) as number[],
});

// An element as a screen reader reaches it: on the Mac its AX role and
// subrole, title, description, and value; on Windows UI Automation's control
// type, localized type, name, full description, and value (Value's, else
// RangeValue's), with the states Narrator says (uia.ts).
export type AccessibleElement = {
	depth: number;
	role: string;
	subrole: string;
	title: string;
	description: string;
	value: string;
} & Partial<
	Pick<
		UiaElement,
		| "toggle"
		| "selected"
		| "expanded"
		| "headingLevel"
		| "position"
		| "level"
		| "dialog"
		| "protected"
		| "rect"
	>
>;

// What a screen reader reaches of the process's windows, element by element
// in its order: VoiceOver's on the Mac (the AX tree); on Windows UI
// Automation's control view, Narrator's (ADR 0071), each visible window of
// the process in turn, its menus among them. It reads only; a locked screen
// hides every window.
export const accessibilityTree = bySystem({
	mac: async (pid: number): Promise<AccessibleElement[]> =>
		JSON.parse(
			await Bun.$`${await swiftTool("ax-tree")} ${String(pid)}`.text(),
		),
	windows: async (pid: number): Promise<AccessibleElement[]> => {
		const elements: AccessibleElement[] = [];
		for (const window of await uiaWindows(pid)) {
			elements.push(...(await uiaTree(window.id)));
		}

		return elements;
	},
});

// An element as one line: its role, the name a screen reader reads (its
// title, else its description) in quotes, and its value: `AXButton "New
// tab"`, `AXTextField "Address" = "https://…"`.
export function describeElement(element: AccessibleElement): string {
	const name = element.title || element.description;
	const value = element.value ? ` = "${element.value}"` : "";

	return `${element.role} "${name}"${value}`;
}

// Turns on the app's full accessibility, as VoiceOver does, then waits a
// moment for Chrome to build its pages' trees.
export async function activateWebAccessibility(pid: number): Promise<void> {
	await Bun.$`${await swiftTool("ax-activate")} ${pid}`.quiet();
	await Bun.sleep(1_000);
}

type Bounds = { X: number; Y: number; Width: number; Height: number };

export type MenuWindow = {
	bounds: Bounds;
	// The screen the menu's top left is on: all of it, and what the menu bar
	// and the Dock leave.
	screen: { frame: Bounds; visible: Bounds };
};

// Views' menu shadow on Windows, around the menu as it is drawn: the window
// holds it, the menu's frame inset by its key shadow's blur, 24, and moved
// down by its offset, 12 (MenuConfig's bubble_menu_shadow_elevation, 12;
// gfx::ShadowValue::MakeMdShadowValues). Views places that frame, not the
// window, at a menu's anchor (MenuController::CalculateBubbleMenuBounds).
const MENU_SHADOW = { left: 24, top: 12, right: 24, bottom: 36 };

// The process's open menus, on screen: each menu's bounds as it is drawn, in
// points from the main display's top left, and its screen's. The Mac's are
// the system's (NSMenu), their windows' frames as the window list writes
// them; Windows' are Views' own (MenuHost), each where its trigger's
// AnchoredMenu put it, its window less its shadow.
export const menusOf = bySystem({
	mac: async (pid: number): Promise<MenuWindow[]> =>
		(
			JSON.parse(
				await Bun.$`${await swiftTool("windows")} --menus ${String(pid)}`.text(),
			) as { windows: MenuWindow[] }
		).windows,
	// Views' menus, each a tool window of Chromium's widget class (MenuHost),
	// in DIPs, as Windows' screen leaves them room (uia.ts).
	windows: async (pid: number): Promise<MenuWindow[]> =>
		(await uiaWindows(pid))
			.filter(
				(window) =>
					window.tool &&
					window.class.startsWith("Chrome_WidgetWin") &&
					window.bounds.Height > 24,
			)
			.map(({ bounds, screen }) => ({
				bounds: {
					X: bounds.X + MENU_SHADOW.left,
					Y: bounds.Y + MENU_SHADOW.top,
					Width: bounds.Width - MENU_SHADOW.left - MENU_SHADOW.right,
					Height: bounds.Height - MENU_SHADOW.top - MENU_SHADOW.bottom,
				},
				screen,
			})),
});

// Where the window's content starts on the screen, in points: what a view's
// frame (the channel's `frames`) is measured from. On the Mac the window's
// own frame; on Windows its client area, inside the frame's resizing edges.
export const contentOrigin = bySystem({
	mac: async (pid: number): Promise<{ x: number; y: number }> => {
		const [window] = (await windowsOf(pid)).windows.sort(
			(a, b) =>
				(b.bounds.Width ?? 0) * (b.bounds.Height ?? 0) -
				(a.bounds.Width ?? 0) * (a.bounds.Height ?? 0),
		);

		return { x: window?.bounds.X ?? 0, y: window?.bounds.Y ?? 0 };
	},
	windows: async (pid: number): Promise<{ x: number; y: number }> => {
		const [window] = (await uiaWindows(pid))
			.filter((each) => !each.tool)
			.sort(
				(a, b) =>
					b.bounds.Width * b.bounds.Height - a.bounds.Width * a.bounds.Height,
			);

		return { x: window?.client.X ?? 0, y: window?.client.Y ?? 0 };
	},
});

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
export const imageDetail = bySystem({
	mac: async (
		file: string,
		region: [number, number, number, number],
	): Promise<{ mean: number; deviation: number }> =>
		JSON.parse(
			await Bun.$`${await swiftTool("image-detail")} ${file} ${region.map(String)}`.text(),
		),
	windows: async (
		file: string,
		region: [number, number, number, number],
	): Promise<{ mean: number; deviation: number }> =>
		JSON.parse(
			await windowsDesktop(
				"[Console]::Write([PaneDesktop]::Detail($env:PANE_FILE, [double]$env:PANE_X0, [double]$env:PANE_Y0, [double]$env:PANE_X1, [double]$env:PANE_Y1))",
				{
					PANE_FILE: file,
					PANE_X0: String(region[0]),
					PANE_Y0: String(region[1]),
					PANE_X1: String(region[2]),
					PANE_Y1: String(region[3]),
				},
			),
		),
});
