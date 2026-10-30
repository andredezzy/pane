import { join } from "node:path";
import { canDriveUI, dialogFieldWidths, mainWindowOf } from "../lib/cua";
import { tabOf } from "../lib/pane";
import { poll } from "../lib/poll";
import { answerSheet, WindowWatcher, waitForSheet } from "../lib/surfaces";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// A server asking for credentials (HTTP Basic) gets the app's dialog: Cancel
// shows the server's refusal, the right credentials sign in.
const httpAuth: Scenario = async (context) => {
	const { app, has } = await launchTraced(context, "http-auth");
	const watcher = new WindowWatcher(app).start();
	const profile = await app.createProfile("Auth");
	const url = context.fixtures.url("auth");
	const tab = await app.openTab(profile, url);

	const sheet = await waitForSheet(app, "authentication");
	expect(sheet.message?.includes("127.0.0.1"), `sheet ${sheet.message}`);
	const cancel = await answerSheet(app, "Cancel");

	const refused = await app.waitFor(
		"the 401 page",
		(state) => !tabOf(state, tab)?.loading,
	);

	await app.send("reload");
	await waitForSheet(app, "authentication");

	// On screen: both fields at their full width, and a picture of the dialog.
	let fields: number[] | "locked" = "locked";

	if (!(await canDriveUI())) {
		context.defer("the dialog's fields at their full width, on screen");
	} else {
		const window = await mainWindowOf(app.browser.pid);

		const wide = (widths: number[]) =>
			widths.length === 2 && widths.every((width) => width >= 200);

		// The dialog's fields reach accessibility a moment after it shows.
		fields = await poll(
			"the dialog's fields",
			async () => {
				const widths = await dialogFieldWidths(app.browser.pid, window);

				return wide(widths) && widths;
			},
			5_000,
			200,
		).catch(() => dialogFieldWidths(app.browser.pid, window));

		// Drawn by the app, not captured from the screen: macOS's AutoFill
		// can float a one-time code from the user's Messages over the field.
		await app.send("render", {
			path: join(context.out, "http-auth-sheet.png"),
		});

		expect(
			wide(fields),
			`the dialog's fields are ${JSON.stringify(fields)} wide`,
		);
	}

	const signIn = await answerSheet(app, "Sign In", ["pane", "secret"]);

	const signedIn = await app.waitFor(
		"the signed-in page",
		(state) => tabOf(state, tab)?.title === "Signed in",
	);

	expect(
		has(
			"PaneWebContentsDelegate.authenticate",
			(args) => args.realm === "Pane fixtures",
		),
		"trace lacks authenticate",
	);

	expect(
		has(
			"PaneAuthenticationChallenge.provide",
			(args) => args.username === "pane",
		),
		"trace lacks the credentials",
	);

	const windows = await watcher.stop();

	expect(
		windows.others.length === 0,
		`Chrome windows appeared: ${JSON.stringify(windows.others)}`,
	);

	return {
		sheet,
		answers: [cancel, signIn],
		fieldWidths: fields,
		refusedTitle: tabOf(refused, tab)?.title,
		signedIn: tabOf(signedIn, tab),
		windows,
	};
};

export default httpAuth;
