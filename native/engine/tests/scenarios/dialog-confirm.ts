import {
	answerSheet,
	pageResult,
	WindowWatcher,
	waitForSheet,
} from "../lib/surfaces";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// confirm() returns the button the user clicked.
const dialogConfirm: Scenario = async (context) => {
	const { app, has } = await launchTraced(context, "dialog-confirm");
	const watcher = new WindowWatcher(app).start();
	const profile = await app.createProfile("Dialogs");
	const url = context.fixtures.url("dialogs.html?kind=confirm");
	const tab = await app.openTab(profile, url);
	const page = await app.page(tab, url);
	const evaluate = (expression: string) => page.evaluate(expression);

	await page.evaluate("runConfirm()");
	const sheet = await waitForSheet(app, "javaScript");
	expect(sheet.buttons?.join(",") === "OK,Cancel", `buttons ${sheet.buttons}`);
	const ok = await answerSheet(app, "OK");
	const accepted = await pageResult<{ value: boolean }>(evaluate);

	await page.evaluate("runConfirm()");
	await waitForSheet(app, "javaScript");
	const cancel = await answerSheet(app, "Cancel");
	const refused = await pageResult<{ value: boolean }>(evaluate);

	expect(accepted.value === true, `OK gave ${accepted.value}`);
	expect(refused.value === false, `Cancel gave ${refused.value}`);

	expect(
		has(
			"PaneWebContentsDelegate.runJavaScriptDialog",
			(args) => args.type === "confirm",
		),
		"trace lacks runJavaScriptDialog(confirm)",
	);

	expect(has("PaneJavaScriptDialog.cancel"), "trace lacks the cancel");

	const windows = await watcher.stop();

	expect(
		windows.others.length === 0,
		`Chrome windows appeared: ${JSON.stringify(windows.others)}`,
	);

	return { sheet, answers: [ok, cancel], accepted, refused, windows };
};

export default dialogConfirm;
