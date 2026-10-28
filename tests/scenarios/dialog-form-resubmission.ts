import { tabOf } from "../lib/pane";
import { answerSheet, WindowWatcher, waitForSheet } from "../lib/surfaces";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// Reloading a page that was posted to asks first; Continue posts again.
const dialogFormResubmission: Scenario = async (context) => {
	const { app, has } = await launchTraced(context, "dialog-form-resubmission");
	const watcher = new WindowWatcher(app).start();
	const profile = await app.createProfile("Dialogs");
	const url = context.fixtures.url("form.html");
	const echo = context.fixtures.url("echo");
	const tab = await app.openTab(profile, url);
	const page = await app.page(tab, url);

	await page.evaluate("document.getElementById('form').submit()");

	await app.waitFor(
		"the posted page",
		(state) => tabOf(state, tab)?.url === echo && !tabOf(state, tab)?.loading,
	);

	await app.send("reload");
	const sheet = await waitForSheet(app, "javaScript");

	expect(
		sheet.title === "Confirm Form Resubmission",
		`sheet title ${sheet.title}`,
	);

	const answer = await answerSheet(app, "Continue");

	await app.waitFor("the second post", (state) => !tabOf(state, tab)?.loading);
	const posted = await app.page(tab, echo);
	const posts = Number(await posted.evaluate<string>("sessionStorage.posts"));

	expect(posts === 2, `the form was posted ${posts} times`);

	expect(
		has(
			"PaneWebContentsDelegate.runJavaScriptDialog",
			(args) => args.type === "formResubmission",
		),
		"trace lacks the form resubmission dialog",
	);

	const windows = await watcher.stop();

	expect(
		windows.others.length === 0,
		`Chrome windows appeared: ${JSON.stringify(windows.others)}`,
	);

	return { sheet, answer, posts, windows };
};

export default dialogFormResubmission;
