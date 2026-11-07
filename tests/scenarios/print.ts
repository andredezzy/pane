import { launchPane } from "../lib/pane";
import { answerModal, WindowWatcher } from "../lib/surfaces";
import { expect, type Scenario } from "../run";

// window.print() and ⌘P open the system print panel (not Chrome's preview);
// Cancel lets the page go on.
const print: Scenario = async (context) => {
	const app = await launchPane(context);
	const watcher = new WindowWatcher(app).start();
	const profile = await app.createProfile("Print");
	const url = context.fixtures.url("print.html");
	const { page } = await app.openPage(profile, url);

	await page.evaluate("startPrint()");

	await app.waitFor("the print panel", (state) => state.modal !== null, 20_000);

	await watcher.sample();
	await answerModal(app, "Cancel");
	await app.waitFor("the panel to close", (state) => state.modal === null);

	const printed = await page.evaluate<{ before: boolean; after: boolean }>(
		"window.printed",
	);

	await app.send("key", { key: "p", modifiers: ["command"] });

	await app.waitFor(
		"the print panel from ⌘P",
		(state) => state.modal !== null,
		20_000,
	);

	await answerModal(app, "Cancel");

	await app.waitFor(
		"the panel to close again",
		(state) => state.modal === null,
	);

	expect(printed.after, `the page saw ${JSON.stringify(printed)}`);

	await watcher.stop();
};

export default print;
