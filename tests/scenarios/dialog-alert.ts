import {
	answerSheet,
	pageResult,
	WindowWatcher,
	waitForSheet,
} from "../lib/surfaces";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// alert() shows as the app's sheet; OK lets the page continue.
const dialogAlert: Scenario = async (context) => {
	const { app, has } = await launchTraced(context, "dialog-alert");
	const watcher = new WindowWatcher(app).start();
	const profile = await app.createProfile("Dialogs");
	const url = context.fixtures.url("dialogs.html?kind=alert");
	const tab = await app.openTab(profile, url);
	const page = await app.page(tab, url);

	await page.evaluate("runAlert()");
	const sheet = await waitForSheet(app, "javaScript");
	await watcher.sample();

	expect(sheet.message === "Pane alert", `sheet message ${sheet.message}`);
	expect(sheet.title === "127.0.0.1 says", `sheet title ${sheet.title}`);

	await answerSheet(app, "OK");

	const result = await pageResult<{ value: unknown }>((expression) =>
		page.evaluate(expression),
	);

	expect("value" in result, "alert() did not return");

	expect(
		has(
			"PaneWebContentsDelegate.runJavaScriptDialog",
			(args) => args.type === "alert",
		),
		"trace lacks runJavaScriptDialog(alert)",
	);

	expect(has("PaneJavaScriptDialog.accept"), "trace lacks the answer");

	await watcher.stop();
};

export default dialogAlert;
