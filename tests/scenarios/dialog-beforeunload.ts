import { tabOf } from "../lib/pane";
import { answerSheet, WindowWatcher, waitForSheet } from "../lib/surfaces";
import { clickAt, launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// A page with a beforeunload handler asks before its tab closes: Cancel keeps
// the tab (and its page), Leave closes it.
const dialogBeforeUnload: Scenario = async (context) => {
	const { app, has } = await launchTraced(context, "dialog-beforeunload");
	const watcher = new WindowWatcher(app).start();
	const profile = await app.createProfile("Dialogs");
	const url = context.fixtures.url("dialogs.html?kind=beforeunload");
	const tab = await app.openTab(profile, url);
	const page = await app.page(tab, url);

	// Chrome asks only on a page the user interacted with.
	await page.evaluate("armBeforeUnload()");
	await clickAt((method, params) => page.send(method, params), 20, 20);

	await app.send("closeTab", { tab });
	const sheet = await waitForSheet(app, "javaScript");
	expect(sheet.title === "Leave site?", `sheet title ${sheet.title}`);
	const stay = await answerSheet(app, "Cancel");

	await Bun.sleep(1_000);
	const kept = await app.state();
	expect(tabOf(kept, tab)?.loaded, "Cancel did not keep the tab");

	expect(
		(await page.evaluate<string>("document.title")) === "Dialogs (armed)",
		"the page went away",
	);

	await app.send("closeTab", { tab });
	await waitForSheet(app, "javaScript");
	const leave = await answerSheet(app, "Leave");

	const closed = await app.waitFor(
		"the tab to close",
		(state) => !tabOf(state, tab),
	);

	expect(
		has(
			"PaneWebContentsDelegate.runJavaScriptDialog",
			(args) => args.type === "beforeunload",
		),
		"trace lacks runJavaScriptDialog(beforeunload)",
	);

	const windows = await watcher.stop();

	expect(
		windows.others.length === 0,
		`Chrome windows appeared: ${JSON.stringify(windows.others)}`,
	);

	return {
		sheet,
		answers: [stay, leave],
		tabsAfter: closed.profiles[0]?.tabs.length,
		windows,
	};
};

export default dialogBeforeUnload;
