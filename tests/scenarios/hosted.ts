import { join } from "node:path";
import {
	canReadUI,
	invokeMenu,
	mainWindowOf,
	pressControl,
} from "../lib/computer-use";
import { bySystem } from "../lib/platform";
import { childrenOf, isRunning, waitForExit } from "../lib/processes";
import { waitForTarget } from "../lib/targets";
import { captureWindow } from "../lib/tools";
import { launchTraced } from "../lib/traced";
import {
	accessibilityTree,
	describeElement,
	imageDetail,
	settledWindowsOf,
} from "../lib/windows";
import { expect, type Scenario } from "../run";

const TITLE = "Pane hosted";

// The app's own controls, by what a screen reader reads (ADR 0071): VoiceOver's
// AX roles on the Mac; on Windows UI Automation's control types, Narrator's,
// the sidebar's edge a slider there, valued at its width.
const READ = bySystem({
	mac: {
		sidebar: [
			'AXButton "New tab"',
			'AXButton "Settings"',
			'AXSplitter "Sidebar width"',
		],
		address: (url: string) => `AXTextField "Address" = "${url}"`,
	},
	windows: {
		sidebar: [
			'Button "New tab"',
			'Button "Settings"',
			'Slider "Sidebar width"',
		],
		address: (url: string) => `Edit "Address" = "${url}"`,
	},
});

// Quit Pane? answered as Quit's own second press does (ADR 0053): its menu
// item again on the Mac, its Exit button on Windows, where the dialog covers
// "Pane ⌄" to a screen reader as it does the eye.
const confirmQuit = bySystem({
	mac: (pid: number) => invokeMenu(pid, ["Pane", "Quit Pane"]),
	windows: (pid: number) => pressControl(pid, "Exit"),
});

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

	if (!(await canReadUI())) {
		context.defer("the page drawn in the window");
		context.defer("Quit Pane stopping every helper");

		return;
	}

	const windowId = await mainWindowOf(browser.pid);

	expect(
		await captureWindow(windowId, join(out, "window.png")),
		`the window ${windowId} was not captured`,
	);

	const tree = (await accessibilityTree(browser.pid))
		.map(describeElement)
		.join("\n");

	// The app's own controls, by the names a screen reader reads (ADR 0071):
	// the sidebar's, and the address field showing the page. Views publishes
	// no identifier of Pane's names (each view's id goes out as its DOM
	// identifier, `view_<n>`); the Swift's identifiers were SwiftUI's.
	expect(
		READ.sidebar.every((control) => tree.includes(control)),
		`accessibility tree lacks the sidebar:\n${tree}`,
	);

	expect(
		tree.includes(READ.address(url)),
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
	// Quit Pane, the menu item ⌘Q runs (Windows' Exit Pane, under "Pane ⌄"),
	// chosen through accessibility. It asks first (Quit Pane?); choosing it
	// again quits, as ⌘Q again does, and on Windows its Exit.
	await invokeMenu(browser.pid, ["Pane", "Quit Pane"]);
	await app.waitFor("Quit Pane?", (current) => current.quitConfirmation);
	await confirmQuit(browser.pid);

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
