import type { AppState } from "../lib/pane";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// A JavaScript dialog pending in a tab that goes is withdrawn: the app hears
// didDismissRequest and drops it, whether the tab closed in the background,
// slept there, or closed while its dialog showed. Nothing answers the page,
// whose frames are gone. Dialogs of the tabs opened after still reach the
// app. A minute passes every second (PANE_TIME_SCALE=60).
const SCALE = 60;

const dialogTabClosed: Scenario = async (context) => {
	const { app, trace, waitForEntry } = await launchTraced(
		context,
		"dialog-tab-closed",
		{ env: { PANE_TIME_SCALE: String(SCALE) } },
	);

	// Nothing sleeps until the sleep case.
	await app.send("setSleepSettings", {
		tabSleepAfterMinutes: 600,
		profileUnloadAfterMinutes: 600,
		cacheBudgetMB: 10_000,
	});

	const profile = await app.createProfile("Dialogs");

	const dismissals = () =>
		trace().filter(
			(entry) =>
				entry.name === "PaneWebContentsDelegate.didDismissRequest" &&
				entry.args.kind === "javaScriptDialog",
		).length;

	const alerts = () =>
		trace().filter(
			(entry) =>
				entry.name === "PaneWebContentsDelegate.runJavaScriptDialog" &&
				entry.args.type === "alert",
		).length;

	// A background tab's alert, waiting in line for its tab to be selected.
	const queueAlert = async (name: string) => {
		const url = context.fixtures.url(`dialogs.html?tab=${name}`);
		const tab = await app.openTab(profile, url);
		const page = await app.page(tab, url);
		const front = context.fixtures.url(`nav-a.html?front-of-${name}`);
		const frontTab = await app.openTab(profile, front);
		await app.page(frontTab, front);
		const before = alerts();
		await page.evaluate("runAlert()");

		await waitForEntry(
			"PaneWebContentsDelegate.runJavaScriptDialog",
			() => alerts() > before,
		);

		await app.waitFor(
			`${name}'s alert in line`,
			(state) => state.pendingRequests === 1 && state.sheet === null,
		);

		return tab;
	};

	const withdrawn = (description: string, count: number) =>
		app.waitFor(
			description,
			(state: AppState) =>
				state.pendingRequests === 0 && dismissals() === count,
			10_000,
		);

	// It closes.
	const closedTab = await queueAlert("closed");
	await app.send("closeTab", { tab: closedTab });
	const afterClose = await withdrawn("the closed tab's alert withdrawn", 1);

	expect(
		!afterClose.profiles.some((p) => p.tabs.some((t) => t.id === closedTab)),
		"the closed tab is still listed",
	);

	// It sleeps (Sleep releases the page, as Unload does).
	const sleptTab = await queueAlert("slept");

	await app.send("setSleepSettings", {
		tabSleepAfterMinutes: 2,
		profileUnloadAfterMinutes: 600,
		cacheBudgetMB: 10_000,
	});

	const afterSleep = await withdrawn("the slept tab's alert withdrawn", 2);

	expect(
		afterSleep.profiles.flatMap((p) => p.tabs).find((t) => t.id === sleptTab)
			?.loaded === false,
		"the tab with the alert did not sleep",
	);

	await app.send("setSleepSettings", {
		tabSleepAfterMinutes: 600,
		profileUnloadAfterMinutes: 600,
		cacheBudgetMB: 10_000,
	});

	// It closes while its alert shows: the app takes the alert down.
	const shownURL = context.fixtures.url("dialogs.html?tab=shown");
	const shownTab = await app.openTab(profile, shownURL);
	const shown = await app.page(shownTab, shownURL);
	await shown.evaluate("runAlert()");

	await app.waitFor(
		"the selected tab's alert shown",
		(state) => state.sheet?.kind === "javaScript",
	);

	await app.send("closeTab", { tab: shownTab });

	const afterShown = await app.waitFor(
		"the shown alert taken down",
		(state) =>
			state.sheet === null && state.pendingRequests === 0 && dismissals() === 3,
		10_000,
	);

	// A tab opened after still gets its dialogs.
	const freshURL = context.fixtures.url("dialogs.html?tab=fresh");
	const freshTab = await app.openTab(profile, freshURL);
	const fresh = await app.page(freshTab, freshURL);
	const before = alerts();
	await fresh.evaluate("runAlert()");

	await waitForEntry(
		"PaneWebContentsDelegate.runJavaScriptDialog",
		() => alerts() > before,
	);

	const freshShown = await app.waitFor(
		"the new tab's alert shown",
		(state) => state.sheet?.kind === "javaScript",
	);

	return {
		dismissals: dismissals(),
		afterClose: { pending: afterClose.pendingRequests },
		afterSleep: { pending: afterSleep.pendingRequests },
		afterShownClosed: {
			pending: afterShown.pendingRequests,
			sheet: afterShown.sheet,
		},
		freshAlert: freshShown.sheet?.message,
	};
};

export default dialogTabClosed;
