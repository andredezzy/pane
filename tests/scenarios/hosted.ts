import { join } from "node:path";
import { canDriveUI, invokeMenu, mainWindowOf, windowState } from "../lib/cua";
import { childrenOf, isRunning, waitForExit } from "../lib/processes";
import { waitForTarget } from "../lib/targets";
import { launchTraced } from "../lib/traced";
import { imageDetail, settledWindowsOf } from "../lib/windows";
import { expect, type Scenario } from "../run";

const TITLE = "Pane hosted";

// Pane's UI runs in the engine it hosts, with Chrome's own UI off (ADR 0010).
// One window of Pane's own, the page it was launched with reported as a
// DevTools page, Pane's window told of its tab in the trace, and — with an
// unlocked screen — the page visible in that window and Quit Pane shutting
// every helper down.
const hosted: Scenario = async (context) => {
	const url = context.fixtures.url("hosted.html");

	const { app, has } = await launchTraced(context, "hosted", {
		args: [url],
	});

	const { browser } = app;
	const { out } = context;

	await waitForTarget(
		browser.cdp,
		`the page ${url} titled ${TITLE}`,
		(target) =>
			target.type === "page" && target.url === url && target.title === TITLE,
	);

	const windows = await settledWindowsOf(browser.pid, 1);

	expect(windows.count === 1, `expected 1 window, got ${windows.count}`);

	expect(
		has("PaneBrowserWindowDelegate.didInsertTab"),
		"trace lacks the window told of its tab (didInsertTab)",
	);

	if (!(await canDriveUI())) {
		context.defer("the page drawn in the window");
		context.defer("Quit Pane stopping every helper");

		return;
	}

	const windowId = await mainWindowOf(browser.pid);

	const state = await windowState(
		browser.pid,
		windowId,
		join(out, "window.png"),
	);

	const tree = state.tree_markdown;

	expect(!state.degraded, `window state degraded: ${state.degraded_reason}`);

	// The app's own controls, by the names VoiceOver reads (ADR 0071): the
	// sidebar's, and the address field showing the page. Views publishes no
	// identifier of Pane's names (each view's id goes out as its DOM
	// identifier, `view_<n>`); the Swift's identifiers were SwiftUI's.
	expect(
		[
			'AXButton "New tab"',
			'AXButton "Settings"',
			'AXSplitter "Sidebar width"',
		].every((control) => tree.includes(control)),
		`accessibility tree lacks the sidebar:\n${tree}`,
	);

	expect(
		tree.includes(`AXTextField "Address" = "${url}"`),
		`the address field does not show the page:\n${tree}`,
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

	const helpersBefore = await childrenOf(browser.pid);
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
};

export default hosted;
