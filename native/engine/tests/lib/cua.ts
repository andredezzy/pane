// Thin wrapper over the cua-driver CLI for checks only the native UI can
// prove: window screenshots, the accessibility tree, real keystrokes.
import { $ } from "bun";
import { swiftTool } from "./tools";

const SESSION = "pane-scenarios";

export type WindowState = {
	window_id: number;
	window_title: string;
	tree_markdown: string;
	degraded?: boolean;
	degraded_reason?: string;
	screenshot_file_path?: string;
};

async function call<T>(
	tool: string,
	args: Record<string, unknown>,
): Promise<T> {
	const run = () =>
		$`cua-driver ${tool} ${JSON.stringify({ session: SESSION, ...args })}`
			.quiet()
			.nothrow();

	let output = await run();

	// The driver ends idle sessions, and only start_session revives a name.
	if (output.stderr.toString().includes("session has ended")) {
		await $`cua-driver start_session ${JSON.stringify({ session: SESSION })}`
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

// macOS hides every window from accessibility and stops delivering input while
// the screen is locked, so visual checks must wait for an unlocked session.
export async function isScreenLocked(): Promise<boolean> {
	const output = await $`ioreg -n Root -d1 -a`.quiet().text();

	return /<key>CGSSessionScreenIsLocked<\/key>\s*<true\/>/.test(output);
}

// The app's largest window. A sheet is a window of its own, and the windows
// come in no fixed order; only the main window holds the sheet's controls.
export async function mainWindowOf(pid: number): Promise<number> {
	const [main] = await windowList(pid);

	if (main === undefined) {
		throw new Error(`no main window for pid ${pid}`);
	}

	return main.window_id;
}

export function windowState(
	pid: number,
	windowId: number,
	screenshot: string,
): Promise<WindowState> {
	return call<WindowState>("get_window_state", {
		pid,
		window_id: windowId,
		max_depth: 12,
		screenshot_out_file: screenshot,
	});
}

// What to click: a control by its title, of `role` (buttons, links, and
// menu items by default); `id` picks one of several with the same title, and
// `inSheet` one in the window's sheet.
export type Control = {
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

type Snapshot = { snapshot_id: string; tree_markdown: string };

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

			const value = found(state.tree_markdown);

			if (value !== undefined) {
				return { state, value, windowId };
			}

			trees.push(`window ${windowId}:\n${withoutMenuBar(state.tree_markdown)}`);
		}

		if (Date.now() > deadline) {
			throw new Error(`no ${what}; ${trees.join("\n")}`);
		}

		await Bun.sleep(200);
	}
}

// Clicks a control in the window, or in whichever of the app's windows has
// it (an app-modal panel is a window of its own), by its accessibility
// element: a real press, as a user's.
export async function clickControl(
	pid: number,
	window: number | "any",
	control: Control,
): Promise<void> {
	const {
		state,
		value: index,
		windowId,
	} = await readUntil(
		pid,
		window,
		{ query: control.title, max_elements: 500 },
		// Only a row that can be pressed: a web button lists no press until
		// Chrome's full accessibility is on, and pressing it then does nothing.
		(tree) =>
			findRow(tree, control)?.match(
				/\[(\d+)\].*actions=\[[^\]]*\bpress\b/,
			)?.[1],
		`control "${control.title}"`,
	);

	await call("click", {
		pid,
		window_id: windowId,
		element_token: `${state.snapshot_id}:${index}`,
	});
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

// Types into the window's text fields, in order.
export async function fillFields(
	pid: number,
	windowId: number,
	texts: string[],
): Promise<void> {
	const { state, value: fields } = await readUntil(
		pid,
		windowId,
		{ max_elements: 120 },
		(tree) => {
			const indices = tree
				.split("\n")
				.filter(
					(line) =>
						/AX(Secure)?TextField/.test(line) && !/pane\.address/.test(line),
				)
				.flatMap((line) => line.match(/\[(\d+)\]/)?.[1] ?? []);

			return indices.length >= texts.length ? indices : undefined;
		},
		`${texts.length} text fields`,
	);

	for (const [position, text] of texts.entries()) {
		await call("set_value", {
			pid,
			window_id: windowId,
			element_token: `${state.snapshot_id}:${fields[position]}`,
			value: text,
		});
	}
}

// Turns on the app's full accessibility, as VoiceOver does, then waits a
// moment for Chrome to build its pages' trees.
export async function activateWebAccessibility(pid: number): Promise<void> {
	await $`${await swiftTool("ax-activate")} ${pid}`.quiet();
	await Bun.sleep(1_000);
}

// The widths of the text fields in the window's sheet.
export async function sheetFieldWidths(
	pid: number,
	windowId: number,
): Promise<number[]> {
	type Element = {
		element_index: number;
		parent_index?: number;
		role: string;
		frame?: { w: number };
	};

	const { elements } = await call<{ elements: Element[] }>("get_window_state", {
		pid,
		window_id: windowId,
		tree_format: "elements",
		max_elements: 500,
		include_screenshot: false,
	});

	const byIndex = new Map(
		elements.map((element) => [element.element_index, element]),
	);

	const inSheet = (element: Element): boolean => {
		const parent =
			element.parent_index === undefined
				? undefined
				: byIndex.get(element.parent_index);

		return !!parent && (parent.role === "AXSheet" || inSheet(parent));
	};

	return elements
		.filter(
			(element) =>
				/AX(Secure)?TextField/.test(element.role) && inSheet(element),
		)
		.map((element) => element.frame?.w ?? 0);
}

// A control's frame and value, by its label and role, in the window's points
// from its top left (clickWindow's), for a real click at a part of it.
export async function controlFrame(
	pid: number,
	windowId: number,
	label: string,
	role: RegExp,
): Promise<{ x: number; y: number; w: number; h: number; value?: string }> {
	type Element = {
		role: string;
		label?: string;
		value?: string;
		frame?: { x: number; y: number; w: number; h: number };
	};

	const { elements } = await call<{ elements: Element[] }>("get_window_state", {
		pid,
		window_id: windowId,
		tree_format: "elements",
		max_elements: 500,
		include_screenshot: false,
	});

	const element = elements.find(
		(each) => each.label === label && role.test(each.role),
	);

	const window = (await windowList(pid)).find(
		(each) => each.window_id === windowId,
	);

	if (!element?.frame || !window) {
		throw new Error(`no ${role} "${label}" in window ${windowId}`);
	}

	return {
		...element.frame,
		x: element.frame.x - window.bounds.x,
		y: element.frame.y - window.bounds.y,
		value: element.value,
	};
}

// Types `text` into the text field with identifier `id`, as typing does (a
// SwiftUI field takes typed text, not a value set from outside).
export async function typeIntoField(
	pid: number,
	windowId: number,
	id: string,
	text: string,
): Promise<void> {
	const { state, value: index } = await readUntil(
		pid,
		windowId,
		{ max_elements: 500 },
		(tree) =>
			tree
				.split("\n")
				.find((line) => /AXTextField/.test(line) && line.includes(`id=${id} `))
				?.match(/\[(\d+)\]/)?.[1],
		`field ${id}`,
	);

	await call("type_text", {
		pid,
		window_id: windowId,
		element_token: `${state.snapshot_id}:${index}`,
		text,
	});
}

// Runs an item of the app's menu bar through accessibility (as choosing it).
export function invokeMenu(pid: number, path: string[]): Promise<unknown> {
	return call("invoke_menu", { pid, path });
}
