import { join } from "node:path";
import {
	invokeMenu,
	isScreenLocked,
	mainWindowOf,
	windowState,
} from "../lib/cua";
import { isRunning, waitForExit } from "../lib/processes";
import { waitForTarget } from "../lib/targets";
import { launchTraced } from "../lib/traced";
import { imageDetail, settledWindowsOf } from "../lib/windows";
import { expect, type Scenario } from "../run";

const URL = "https://example.com/";
const TITLE = "Example Domain";

// The app's helper processes: its children.
async function helpersOf(pid: number): Promise<number[]> {
	const output = await Bun.$`pgrep -P ${pid}`.quiet().nothrow().text();

	return output.split("\n").filter(Boolean).map(Number);
}

// Phase 1: the Pane app hosts the engine. One window of the app's own, the
// page reported as a DevTools page, the app↔engine traffic in the trace, and
// — with an unlocked screen — the page visible in that window and Cmd+Q
// shutting every helper down.
const hosted: Scenario = async (context) => {
	const { app, trace, has } = await launchTraced(context, "hosted", {
		args: [URL],
	});

	const { browser } = app;
	const { out } = context;

	const page = await waitForTarget(
		browser.cdp,
		`the page ${URL} titled ${TITLE}`,
		(target) =>
			target.type === "page" && target.url === URL && target.title === TITLE,
	);

	const windows = await settledWindowsOf(browser.pid, 1);

	expect(windows.count === 1, `expected 1 window, got ${windows.count}`);
	expect(has("PaneBrowserWindow.openTab"), "trace lacks openTab");

	expect(
		has(
			"PaneWebContentsDelegate.didChangeTitle",
			(args) => args.title === TITLE,
		),
		`trace lacks didChangeTitle(${TITLE})`,
	);

	const traceEntries = trace().length;

	if (await isScreenLocked()) {
		context.defer("the page drawn in the window");
		context.defer("Quit Pane stopping every helper");

		return { page, windows, traceEntries };
	}

	const windowId = await mainWindowOf(browser.pid);

	const state = await windowState(
		browser.pid,
		windowId,
		join(out, "window.png"),
	);

	const tree = state.tree_markdown;

	expect(!state.degraded, `window state degraded: ${state.degraded_reason}`);

	// The app's own controls, by their accessibility identifiers.
	expect(
		/id=pane\.sidebar\./.test(tree),
		`accessibility tree lacks the sidebar:\n${tree}`,
	);

	expect(
		new RegExp(`AXTextField = "${URL.replace(/[./]/g, "\\$&")}"`).test(tree),
		"the address field does not show the page",
	);

	// The page drawn in the window: its content area is not a flat color.
	const detail = await imageDetail(
		join(out, "window.png"),
		[0.3, 0.2, 0.95, 0.8],
	);

	expect(
		detail.deviation > 0.05,
		`the page area is blank (luminance deviation ${detail.deviation})`,
	);

	const helpersBefore = await helpersOf(browser.pid);
	// Quit Pane, the menu item ⌘Q runs, chosen through accessibility. It
	// asks first (Quit Pane?); choosing it again quits, as ⌘Q again does.
	await invokeMenu(browser.pid, ["Pane", "Quit Pane"]);
	await app.waitFor("Quit Pane?", (current) => current.quitConfirmation);
	await invokeMenu(browser.pid, ["Pane", "Quit Pane"]);

	const exited = await waitForExit(browser.pid, 5_000);

	// This app's helpers only: other scenarios' may still be exiting.
	await Promise.all(helpersBefore.map((helper) => waitForExit(helper, 5_000)));
	const helpersAfter = helpersBefore.filter(isRunning);

	expect(exited, "the app still runs 5 s after Quit");
	expect(helpersBefore.length > 0, "the app had no helper processes");

	expect(
		helpersAfter.length === 0,
		`helpers still running after Quit: ${helpersAfter.join(", ")}`,
	);

	return {
		page,
		windows,
		traceEntries,
		windowTitle: state.window_title,
		pageDetail: detail,
		accessibility: tree,
		quit: { helpersBefore: helpersBefore.length, exited, helpersAfter: 0 },
	};
};

export default hosted;
