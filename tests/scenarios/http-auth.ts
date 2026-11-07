import { canDriveUI, dialogFieldWidths, mainWindowOf } from "../lib/cua";
import { launchPane, tabOf } from "../lib/pane";
import { poll } from "../lib/poll";
import { answerSheet, WindowWatcher, waitForSheet } from "../lib/surfaces";
import { expect, type Scenario } from "../run";

// A server asking for credentials (HTTP Basic) gets the app's dialog: Cancel
// shows the server's refusal, the right credentials sign in.
const httpAuth: Scenario = async (context) => {
	const app = await launchPane(context);
	const watcher = new WindowWatcher(app).start();
	const profile = await app.createProfile("Auth");
	const url = context.fixtures.url("auth");
	const tab = await app.openTab(profile, url);

	const sheet = await waitForSheet(app, "authentication");
	expect(sheet.message?.includes("127.0.0.1"), `sheet ${sheet.message}`);
	await answerSheet(app, "Cancel");

	await app.waitFor("the 401 page", (state) => !tabOf(state, tab)?.loading);

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
		await app.render("http-auth-sheet.png", 0);

		expect(
			wide(fields),
			`the dialog's fields are ${JSON.stringify(fields)} wide`,
		);
	}

	await answerSheet(app, "Sign In", ["pane", "secret"]);

	await app.waitFor(
		"the signed-in page",
		(state) => tabOf(state, tab)?.title === "Signed in",
	);

	await watcher.stop();
};

export default httpAuth;
