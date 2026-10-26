// Thin wrapper over the cua-driver CLI for checks only the native UI can
// prove: window screenshots, the accessibility tree, real keystrokes.
import { $ } from "bun";

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
	const output =
		await $`cua-driver ${tool} ${JSON.stringify({ session: SESSION, ...args })}`
			.quiet()
			.nothrow();

	return JSON.parse(output.stdout.toString()) as T;
}

// macOS hides every window from accessibility and stops delivering input while
// the screen is locked, so visual checks must wait for an unlocked session.
export async function isScreenLocked(): Promise<boolean> {
	const output = await $`ioreg -n Root -d1 -a`.quiet().text();

	return /<key>CGSSessionScreenIsLocked<\/key>\s*<true\/>/.test(output);
}

export async function mainWindowOf(pid: number): Promise<number> {
	const { windows } = await call<{
		windows: { window_id: number; bounds: { height: number } }[];
	}>("list_windows", { pid });

	const [main] = windows.filter((window) => window.bounds.height > 200);

	if (!main) {
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

export function hotkey(
	pid: number,
	windowId: number,
	keys: string[],
): Promise<unknown> {
	return call("hotkey", {
		pid,
		window_id: windowId,
		keys,
		delivery_mode: "foreground",
	});
}
