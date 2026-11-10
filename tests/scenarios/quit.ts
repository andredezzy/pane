import { launchPane, type PaneApp, pressWindowKey } from "../lib/pane";
import { waitForExit } from "../lib/processes";
import { expect, type Scenario, type ScenarioContext } from "../run";

// ⌘Q asks first, as Aside does (Paper: 04.5 Quit Pane). From a focused page:
// ⌘Q shows Quit Pane?, Esc goes back and gives the page its focus again, and
// ⌘Q then Return quits. On the Dark theme, ⌘Q twice quits. ⌘Q is posted into
// the app's event queue, so it takes the menu bar's real path, which needs
// no front. Esc and Return are the window's keys: with the front, posted as
// ⌘Q is; without it, through Views' own dispatch to what has the keys.

async function focusedPage(
	context: ScenarioContext,
	app: PaneApp,
	url: string,
): Promise<void> {
	const profile = await app.createProfile("Quit");
	await app.openPage(profile, url);

	if (context.foreground) {
		await app.send("activate");
		await app.waitFor("the key window", (state) => state.window.key, 10_000);
	}

	await app.send("clickPage", { x: 20, y: 20 });

	await app.waitFor(
		"the page focused",
		(state) => state.focus === "pane.content",
	);
}

const quit: Scenario = async (context) => {
	const url = context.fixtures.url("smoke.html?quit");

	// ⌘Q, Esc, then ⌘Q and Return.
	const app = await launchPane(context);
	await focusedPage(context, app, url);

	await app.send("key", { key: "q", modifiers: ["command"] });

	await app.waitFor("Quit Pane?", (state) => state.quitConfirmation);

	await app.render("quit-light.png");

	await pressWindowKey(context, app, "\u001b");

	await app.waitFor(
		"Quit Pane? gone, the page focused again",
		(state) => !state.quitConfirmation && state.focus === "pane.content",
	);

	await app.send("key", { key: "q", modifiers: ["command"] });
	await app.waitFor("Quit Pane? again", (state) => state.quitConfirmation);
	await pressWindowKey(context, app, "\r");
	const quitByReturn = await waitForExit(app.browser.pid);

	expect(quitByReturn, "Return did not quit");

	// The Dark theme, ⌘Q twice.
	const dark = await launchPane(context);
	await dark.send("theme", { theme: "dark" });
	await focusedPage(context, dark, url);

	await dark.send("key", { key: "q", modifiers: ["command"] });
	await dark.waitFor("Quit Pane?", (state) => state.quitConfirmation);
	await dark.render("quit-dark.png");

	await dark.send("key", { key: "q", modifiers: ["command"] });
	const quitByCommandQ = await waitForExit(dark.browser.pid);

	expect(quitByCommandQ, "⌘Q twice did not quit");
};

export default quit;
