// Thin wrapper over the cua-driver CLI for checks only the native UI can
// prove: window screenshots, the accessibility tree, real keystrokes. It
// reads macOS's accessibility tree (AXButton, AXSheet): Windows' is UI
// Automation's, which W8 checks with Narrator (ADR 0071), so there the UI is
// never driven (canDriveUI) and a call to the driver throws.
import { bySystem, noWindowsSide } from "./platform";
import { captureWindow, swiftTool } from "./tools";

const SESSION = "pane-scenarios";

export type WindowState = {
	window_id: number;
	window_title: string;
	tree_markdown: string;
	degraded?: boolean;
	degraded_reason?: string;
	screenshot_file_path?: string;
};

async function callOnMac<T>(
	tool: string,
	args: Record<string, unknown>,
): Promise<T> {
	const run = () =>
		Bun.$`cua-driver ${tool} ${JSON.stringify({ session: SESSION, ...args })}`
			.quiet()
			.nothrow();

	let output = await run();

	// The driver ends idle sessions, and only start_session revives a name.
	if (output.stderr.toString().includes("session has ended")) {
		await Bun.$`cua-driver start_session ${JSON.stringify({ session: SESSION })}`
			.quiet()
			.nothrow();

		output = await run();
	}

	const text = output.stdout.toString();

	try {
		return JSON.parse(text) as T;
	} catch {
		throw new Error(
			`cua-driver ${tool}: ${text.trim() || output.stderr.toString().trim()}`,
		);
	}
}

export const call: <T>(
	tool: string,
	args: Record<string, unknown>,
) => Promise<T> = bySystem({
	mac: callOnMac,
	windows: async (tool: string) => {
		throw noWindowsSide(
			`cua-driver's ${tool} on macOS's accessibility tree`,
			"W8",
			"Windows' is UI Automation's, checked with Narrator (ADR 0071)",
		);
	},
});

// macOS hides every window from accessibility and stops delivering input while
// the screen is locked, so visual checks must wait for an unlocked session.
// Windows shows its lock and sign-in screens through LogonUI.exe, which runs
// only while one shows, and Chromium counts its windows occluded meanwhile.
export const isScreenLocked = bySystem({
	mac: async () => {
		const output = await Bun.$`ioreg -n Root -d1 -a`.quiet().text();

		return /<key>CGSSessionScreenIsLocked<\/key>\s*<true\/>/.test(output);
	},
	windows: async () => {
		const list = Bun.spawn(
			["tasklist", "/FI", "IMAGENAME eq LogonUI.exe", "/FO", "CSV", "/NH"],
			{ stdout: "pipe", stderr: "ignore" },
		);

		return (await new Response(list.stdout).text()).includes('"LogonUI.exe"');
	},
});

// Whether checks of the native UI can run: the screen unlocked, and
// cua-driver installed (GitHub's runners have none) with its daemon up.
// Never on Windows, whose tree this file doesn't read.
export const canDriveUI = bySystem({
	mac: async () => {
		if (Bun.which("cua-driver") === null || (await isScreenLocked())) {
			return false;
		}

		// It says so on stderr, and exits 0 either way.
		const status = await Bun.$`cua-driver status`.quiet().nothrow();

		return !`${status.stdout}${status.stderr}`.includes("not running");
	},
	windows: async () => false,
});

// The app's largest window. A sheet is a window of its own, and the windows
// come in no fixed order; only the main window holds the sheet's controls.
export async function mainWindowOf(pid: number): Promise<number> {
	const [main] = await windowList(pid);

	if (main === undefined) {
		throw new Error(`no main window for pid ${pid}`);
	}

	return main.window_id;
}

export async function windowState(
	pid: number,
	windowId: number,
	screenshot: string,
): Promise<WindowState> {
	const state = await call<WindowState>("get_window_state", {
		pid,
		window_id: windowId,
		max_depth: 12,
		screenshot_out_file: screenshot,
	});

	// The driver's own capture waits for a consent only a person can give
	// (`cua-driver permissions grant`): the system's takes the window as well,
	// without the front.
	if (!state.screenshot_file_path) {
		await captureWindow(windowId, screenshot);
	}

	return state;
}

// What to click: a control by its title, of `role` (buttons, links, and
// menu items by default); `id` picks one of several with the same title, and
// `inSheet` one in the window's sheet.
type Control = {
	title: string;
	role?: RegExp;
	id?: string;
	inSheet?: boolean;
};

// A row of cua-driver's tree: `- [N] AXRole (Label) [attributes]`, or
// `- [N] AXRole "Title"`; its element token is `<snapshot_id>:N`.
function findRow(tree: string, control: Control): string | undefined {
	const { title, id } = control;
	const role = control.role ?? /AX(Button|Link|MenuItem)/;
	const escaped = title.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
	const label = new RegExp(`(\\(${escaped}(,[^)]*)?\\)|"${escaped}")`);
	const lines = tree.split("\n");

	const from = control.inSheet
		? lines.findIndex((line) => /AXSheet\b/.test(line))
		: 0;

	if (from === -1) {
		return undefined;
	}

	return lines
		.slice(from)
		.find(
			(line) =>
				role.test(line) &&
				label.test(line) &&
				(!id || line.includes(`id=${id} `) || line.includes(`id=${id}]`)),
		);
}

// A window that goes between the list and the read (a menu closing) has no
// tree.
type Snapshot = { snapshot_id: string; tree_markdown?: string };

type ListedWindow = {
	window_id: number;
	bounds: { x: number; y: number; height: number; width: number };
};

// The app's windows, largest first.
async function windowList(pid: number): Promise<ListedWindow[]> {
	const { windows } = await call<{ windows: ListedWindow[] }>("list_windows", {
		pid,
	});

	const area = (window: ListedWindow) =>
		window.bounds.width * window.bounds.height;

	return windows.sort((a, b) => area(b) - area(a));
}

// A tree without the menu bar, which lists the user's recent items and
// other apps: failures quote the window, never that.
function withoutMenuBar(tree: string): string {
	const lines = tree.split("\n");

	const start = lines.findIndex((line) =>
		/^\s*- (\[\d+\] )?AXMenuBar\b/.test(line),
	);

	if (start === -1) {
		return tree;
	}

	const indent = (line: string) => line.search(/\S/);
	const level = indent(lines[start] ?? "");
	let end = start + 1;

	while (end < lines.length && indent(lines[end] ?? "") > level) {
		end++;
	}

	return [...lines.slice(0, start), ...lines.slice(end)].join("\n");
}

// Reads the window (or each of the app's windows) until `found` picks
// something out of it: a sheet is in the app's state a moment before its
// controls reach accessibility.
async function readUntil<T>(
	pid: number,
	window: number | "any",
	args: Record<string, unknown>,
	found: (tree: string) => T | undefined,
	what: string,
): Promise<{ state: Snapshot; value: T; windowId: number }> {
	const deadline = Date.now() + 5_000;

	for (;;) {
		const windowIds =
			window === "any"
				? (await windowList(pid)).map((listed) => listed.window_id)
				: [window];

		const trees: string[] = [];

		for (const windowId of windowIds) {
			const state = await call<Snapshot>("get_window_state", {
				pid,
				window_id: windowId,
				...args,
			});

			const tree = state.tree_markdown ?? "";
			const value = found(tree);

			if (value !== undefined) {
				return { state, value, windowId };
			}

			trees.push(`window ${windowId}:\n${withoutMenuBar(tree)}`);
		}

		if (Date.now() > deadline) {
			throw new Error(`no ${what}; ${trees.join("\n")}`);
		}

		await Bun.sleep(200);
	}
}

// Clicks a control in the window, or in whichever of the app's windows has
// it (an app-modal panel is a window of its own), by its accessibility
// element: a real press, as a user's. The element can be built anew
// between the read and the press (kAXErrorInvalidUIElement, -25202), as
// SwiftUI's were and a page's can be: it is read again, up to three times.
async function clickControl(
	pid: number,
	window: number | "any",
	control: Control,
): Promise<void> {
	for (let attempt = 1; ; attempt++) {
		const {
			state,
			value: index,
			windowId,
		} = await readUntil(
			pid,
			window,
			{ query: control.title, max_elements: 500 },
			// Only a row that can be pressed: a web button lists no press until
			// Chrome's full accessibility is on, and pressing it then does
			// nothing.
			(tree) =>
				findRow(tree, control)?.match(
					/\[(\d+)\].*actions=\[[^\]]*\bpress\b/,
				)?.[1],
			`control "${control.title}"`,
		);

		try {
			await call("click", {
				pid,
				window_id: windowId,
				element_token: `${state.snapshot_id}:${index}`,
			});

			return;
		} catch (error) {
			if (attempt >= 3 || !String(error).includes("-25202")) {
				throw error;
			}
		}
	}
}

// Clicks the button titled `title`; `id` picks one of several.
export function clickButton(
	pid: number,
	window: number | "any",
	title: string,
	id?: string,
): Promise<void> {
	return clickControl(pid, window, { title, id });
}

// Turns on the app's full accessibility, as VoiceOver does, then waits a
// moment for Chrome to build its pages' trees.
export async function activateWebAccessibility(pid: number): Promise<void> {
	await Bun.$`${await swiftTool("ax-activate")} ${pid}`.quiet();
	await Bun.sleep(1_000);
}

// Runs an item of the app's menu bar through accessibility (as choosing it).
export function invokeMenu(pid: number, path: string[]): Promise<unknown> {
	return call("invoke_menu", { pid, path });
}
