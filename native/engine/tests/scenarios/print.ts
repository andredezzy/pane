import { answerModal, WindowWatcher } from "../lib/surfaces";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// window.print() and ⌘P open the system print panel (not Chrome's preview);
// Cancel lets the page go on.
const print: Scenario = async (context) => {
	const { app, has } = await launchTraced(context, "print");
	const watcher = new WindowWatcher(app).start();
	const profile = await app.createProfile("Print");
	const url = context.fixtures.url("print.html");
	const tab = await app.openTab(profile, url);
	const page = await app.page(tab, url);

	await page.evaluate("startPrint()");

	const scripted = await app.waitFor(
		"the print panel",
		(state) => state.modal !== null,
		20_000,
	);

	await watcher.sample("print panel");
	const cancel = await answerModal(app, "Cancel");
	await app.waitFor("the panel to close", (state) => state.modal === null);

	const printed = await page.evaluate<{ before: boolean; after: boolean }>(
		"window.printed",
	);

	await app.send("key", { key: "p", modifiers: ["command"] });

	const fromMenu = await app.waitFor(
		"the print panel from ⌘P",
		(state) => state.modal !== null,
		20_000,
	);

	const cancelAgain = await answerModal(app, "Cancel");

	await app.waitFor(
		"the panel to close again",
		(state) => state.modal === null,
	);

	expect(printed.after, `the page saw ${JSON.stringify(printed)}`);
	expect(has("PaneWebContentsDelegate.willPrint"), "trace lacks willPrint");
	expect(has("PaneWebContents.print"), "trace lacks the ⌘P print");

	const windows = await watcher.stop();

	expect(
		windows.others.length === 0,
		`Chrome windows appeared: ${JSON.stringify(windows.others)}`,
	);

	return {
		panel: scripted.modal,
		menuPanel: fromMenu.modal,
		answers: [cancel, cancelAgain],
		printed,
		windows,
	};
};

export default print;
