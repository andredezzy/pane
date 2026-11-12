import { launchPane, pressWindowKey, tabOf } from "../lib/pane";
import {
	answerSheet,
	pageResult,
	WindowWatcher,
	waitForSheet,
} from "../lib/surfaces";
import { clickAt } from "../lib/traced";
import { expect, type Scenario } from "../run";

// A page's dialogs show in the app's window (ADR 0021), on one launch, each
// page in a tab of its own: alert() continues on OK; confirm() returns the
// button clicked, or Return's or Esc's; prompt() returns what was typed, or
// null; beforeunload asks before its tab closes; and reloading what was
// posted asks first.
const dialogs: Scenario = async (context) => {
	const app = await launchPane(context);
	const watcher = new WindowWatcher(app).start();
	const profile = await app.createProfile("Dialogs");

	const openDialogs = async (kind: string) => {
		const url = context.fixtures.url(`dialogs.html?kind=${kind}`);
		const { tab, page } = await app.openPage(profile, url);

		return {
			tab,
			page,
			result: <T>() => pageResult<T>((expression) => page.evaluate(expression)),
		};
	};

	// alert()
	const alert = await openDialogs("alert");
	await alert.page.evaluate("runAlert()");
	const alertSheet = await waitForSheet(app, "javaScript");
	await watcher.sample();

	expect(
		alertSheet.message === "Pane alert",
		`sheet message ${alertSheet.message}`,
	);

	expect(
		alertSheet.title === "127.0.0.1 says",
		`sheet title ${alertSheet.title}`,
	);

	await answerSheet(app, "OK");
	const alerted = await alert.result<{ value: unknown }>();
	expect("value" in alerted, "alert() did not return");

	// confirm()
	const confirm = await openDialogs("confirm");
	await confirm.page.evaluate("runConfirm()");
	const confirmSheet = await waitForSheet(app, "javaScript");

	expect(
		confirmSheet.buttons?.join(",") === "OK,Cancel",
		`buttons ${confirmSheet.buttons}`,
	);

	await answerSheet(app, "OK");
	const accepted = await confirm.result<{ value: boolean }>();
	await confirm.page.evaluate("runConfirm()");
	await waitForSheet(app, "javaScript");
	await answerSheet(app, "Cancel");
	const refused = await confirm.result<{ value: boolean }>();

	// Return presses OK and Esc dismisses, as Chrome's dialogs take them: the
	// dialog's own keys, as the window hands them to it.
	const keys: Record<string, boolean> = {};

	for (const [name, key] of [
		["return", "\r"],
		["escape", "\u001b"],
	] as const) {
		await confirm.page.evaluate("runConfirm()");
		await waitForSheet(app, "javaScript");
		await pressWindowKey(context, app, key);
		keys[name] = (await confirm.result<{ value: boolean }>()).value;
	}

	expect(
		keys.return === true && keys.escape === false,
		`keys gave ${JSON.stringify(keys)}`,
	);

	expect(accepted.value === true, `OK gave ${accepted.value}`);
	expect(refused.value === false, `Cancel gave ${refused.value}`);

	// prompt()
	const prompt = await openDialogs("prompt");
	await prompt.page.evaluate("runPrompt()");
	const promptSheet = await waitForSheet(app, "javaScript");

	expect(
		promptSheet.fields?.[0] === "default text",
		`prompt field ${promptSheet.fields}`,
	);

	await answerSheet(app, "OK", ["Typed in Pane"]);
	const typed = await prompt.result<{ value: string | null }>();
	await prompt.page.evaluate("runPrompt()");
	await waitForSheet(app, "javaScript");
	await answerSheet(app, "Cancel");
	const cancelled = await prompt.result<{ value: string | null }>();

	expect(
		typed.value === "Typed in Pane",
		`prompt gave ${JSON.stringify(typed.value)}`,
	);

	expect(
		cancelled.value === null,
		`Cancel gave ${JSON.stringify(cancelled.value)}`,
	);

	// Typed into the prompt by real keys, through AppKit's queue, its default
	// text selected and replaced: the Mac hands each key to the views before
	// the field's text input inserts it, and the dialog had kept every one.
	if (context.foreground) {
		await prompt.page.evaluate("runPrompt()");
		await waitForSheet(app, "javaScript");
		await app.send("activate");
		await app.waitFor("the key window", (state) => state.window.key, 10_000);

		for (const key of "quilt") {
			await app.send("key", { key });
		}

		await app.waitFor(
			"the typed text in the prompt's field",
			(state) => state.sheet?.fields?.[0] === "quilt",
			5_000,
		);

		await pressWindowKey(context, app, "\r");
		const keyed = await prompt.result<{ value: string | null }>();

		expect(
			keyed.value === "quilt",
			`the prompt typed into gave ${JSON.stringify(keyed.value)}`,
		);
	} else {
		context.defer(
			"typing into the prompt by real keys: it needs Pane in front (run.ts --foreground)",
		);
	}

	// beforeunload: Cancel keeps the tab (and its page), Leave closes it. Chrome
	// asks only on a page the user interacted with.
	const leaving = await openDialogs("beforeunload");
	await leaving.page.evaluate("armBeforeUnload()");

	await clickAt((method, params) => leaving.page.send(method, params), 20, 20);

	await app.send("closeTab", { tab: leaving.tab });
	const leaveSheet = await waitForSheet(app, "javaScript");
	expect(leaveSheet.title === "Leave site?", `sheet title ${leaveSheet.title}`);
	await answerSheet(app, "Cancel");
	await Bun.sleep(1_000);
	const kept = await app.state();
	expect(tabOf(kept, leaving.tab)?.loaded, "Cancel did not keep the tab");

	expect(
		(await leaving.page.evaluate<string>("document.title")) ===
			"Dialogs (armed)",
		"the page went away",
	);

	await app.send("closeTab", { tab: leaving.tab });
	await waitForSheet(app, "javaScript");
	await answerSheet(app, "Leave");
	await app.waitFor("the tab to close", (state) => !tabOf(state, leaving.tab));

	// Reloading a page that was posted to asks first; Continue posts again.
	const echo = context.fixtures.url("echo");

	const { tab, page } = await app.openPage(
		profile,
		context.fixtures.url("form.html"),
	);

	await page.evaluate("document.getElementById('form').submit()");

	await app.waitFor(
		"the posted page",
		(state) => tabOf(state, tab)?.url === echo && !tabOf(state, tab)?.loading,
	);

	await app.send("reload");
	const resubmit = await waitForSheet(app, "javaScript");

	expect(
		resubmit.title === "Confirm Form Resubmission",
		`sheet title ${resubmit.title}`,
	);

	await answerSheet(app, "Continue");
	await app.waitFor("the second post", (state) => !tabOf(state, tab)?.loading);
	const posted = await app.page(tab, echo);
	const posts = Number(await posted.evaluate<string>("sessionStorage.posts"));
	expect(posts === 2, `the form was posted ${posts} times`);

	await watcher.stop();
};

export default dialogs;
