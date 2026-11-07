import {
	answerSheet,
	pageResult,
	WindowWatcher,
	waitForSheet,
} from "../lib/surfaces";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// prompt() returns what the user typed, or null on Cancel.
const dialogPrompt: Scenario = async (context) => {
	const { app, has } = await launchTraced(context, "dialog-prompt");
	const watcher = new WindowWatcher(app).start();
	const profile = await app.createProfile("Dialogs");
	const url = context.fixtures.url("dialogs.html?kind=prompt");
	const { page } = await app.openPage(profile, url);
	const evaluate = (expression: string) => page.evaluate(expression);

	await page.evaluate("runPrompt()");
	const sheet = await waitForSheet(app, "javaScript");
	expect(sheet.fields?.[0] === "default text", `prompt field ${sheet.fields}`);
	await answerSheet(app, "OK", ["Typed in Pane"]);
	const typed = await pageResult<{ value: string | null }>(evaluate);

	await page.evaluate("runPrompt()");
	await waitForSheet(app, "javaScript");
	await answerSheet(app, "Cancel");
	const cancelled = await pageResult<{ value: string | null }>(evaluate);

	expect(
		typed.value === "Typed in Pane",
		`prompt gave ${JSON.stringify(typed.value)}`,
	);

	expect(
		cancelled.value === null,
		`Cancel gave ${JSON.stringify(cancelled.value)}`,
	);

	expect(
		has("PaneJavaScriptDialog.accept", (args) => args.text === "Typed in Pane"),
		"trace lacks the typed answer",
	);

	await watcher.stop();
};

export default dialogPrompt;
