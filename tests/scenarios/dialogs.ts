import { bringToFront, launchPane, pressWindowKey, tabOf } from "../lib/pane";
import { bySystem } from "../lib/platform";
import { pollValue } from "../lib/poll";
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
// A page's dialog on Windows is the card of Pane's own questions (Paper
// 04.15.1 to 04.23.1, 11.18); the Mac's, its alert sheet.
const PANES_OWN_DIALOG = bySystem({ mac: false, windows: true });

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

	// On Windows the card of Pane's own questions over the page's dim, 480
	// wide, centred on the page it dims, not the window (Paper 11.18); its
	// buttons keyed, the default with Enter, Cancel with Esc; Tab goes round
	// them alone, and Enter presses the one with the ring.
	if (PANES_OWN_DIALOG) {
		await confirm.page.evaluate("runConfirm()");
		await waitForSheet(app, "javaScript");

		// Measured once it has come: it grows into place as it shows.
		const frames = await pollValue(
			async () =>
				(
					await app.send<Record<string, number[]>>("frames", {
						prefix: "pane.dialog",
					})
				).result,
			(measured) => measured["pane.dialog"]?.[2] === 432,
			2_000,
		);

		const { page } = (await app.send<{ page: number[] }>("modalHost")).result;
		const [x = 0, , width = 0] = frames["pane.dialog"] ?? [];
		const [pageX = 0, , pageWidth = 0] = page;

		expect(
			width === 432 && Math.abs(x + width / 2 - (pageX + pageWidth / 2)) <= 1,
			`the dialog's content at ${frames["pane.dialog"]}, the page at ${page}`,
		);

		await app.render("dialog-confirm.png", 300);
		await answerSheet(app, "Cancel");
		await confirm.result<{ value: boolean }>();

		if (await bringToFront(context, app, "a dialog's buttons by the keys")) {
			// Cancel, the first Tab's, then OK; each pressed by Enter.
			const pressedAfter = async (tabs: number) => {
				await confirm.page.evaluate("runConfirm()");
				await waitForSheet(app, "javaScript");

				for (let step = 0; step < tabs; step++) {
					await app.send("key", { key: "\t" });
					await Bun.sleep(200);
				}

				const focus = (await app.state()).focus;
				await app.send("key", { key: "\r" });

				return {
					focus,
					value: (await confirm.result<{ value: boolean }>()).value,
				};
			};

			const onCancel = await pressedAfter(1);
			const onOk = await pressedAfter(2);
			const wrapped = await pressedAfter(3);

			expect(
				onCancel.focus === "pane.dialog.button" &&
					onCancel.value === false &&
					onOk.value === true &&
					wrapped.focus === "pane.dialog.button" &&
					wrapped.value === false,
				`Tab and Enter in the dialog: ${JSON.stringify({ onCancel, onOk, wrapped })}`,
			);
		}
	}

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
