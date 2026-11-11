import { launchPane, tabOf } from "../lib/pane";
import { answerSheet, WindowWatcher, waitForSheet } from "../lib/surfaces";
import { expect, type Scenario } from "../run";

// A server asking for credentials (HTTP Basic) gets the app's dialog: Cancel
// shows the server's refusal, the right credentials sign in. Its two fields
// take the dialog's whole width, 222 points (ADR 0021).
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

	// The card grows from 0.97 as it comes (Motion.panel, 0.15 s), and its
	// frames take that scale: measured once it has.
	await Bun.sleep(250);

	// Both fields at their full width, and a picture of the dialog, drawn by
	// the app, not captured from the screen: macOS's AutoFill can float a
	// one-time code from the user's Messages over the field.
	const fields = Object.entries(await app.frames("pane.dialog.field."))
		.sort(([a], [b]) => a.localeCompare(b))
		.map(([, [, , width]]) => width);

	await app.render("http-auth-sheet.png");

	expect(
		fields.length === 2 && fields.every((width) => width === 222),
		`the dialog's fields are ${JSON.stringify(fields)} wide`,
	);

	await answerSheet(app, "Sign In", ["pane", "secret"]);

	await app.waitFor(
		"the signed-in page",
		(state) => tabOf(state, tab)?.title === "Signed in",
	);

	await watcher.stop();
};

export default httpAuth;
