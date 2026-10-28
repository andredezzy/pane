import { join } from "node:path";
import { launchPane, type PaneApp } from "../lib/pane";
import { waitForExit } from "../lib/processes";
import { expect, type Scenario } from "../run";

// ⌘Q asks first, as Aside does (Paper: 04.5 Quit Pane). From a focused page:
// ⌘Q shows Quit Pane?, Esc goes back and gives the page its focus again, and
// ⌘Q then Return quits. On the Dark theme, ⌘Q twice quits. Keys are posted
// into the app's event queue, so they take the menu's real path.

// A key the panel takes needs the key window, as when the user is in Pane;
// another app may have taken it during the run.
async function press(app: PaneApp, key: string): Promise<void> {
	await app.send("activate");
	await app.waitFor("the key window", (state) => state.window.key, 10_000);
	await app.send("key", { key });
}

async function focusedPage(app: PaneApp, url: string): Promise<void> {
	const profile = await app.createProfile("Quit");
	await app.page(await app.openTab(profile, url), url);
	await app.send("activate");
	await app.waitFor("the key window", (state) => state.window.key, 10_000);
	await app.send("clickPage", { x: 20, y: 20 });

	await app.waitFor(
		"the page focused",
		(state) => state.focus === "RenderWidgetHostViewCocoa",
	);
}

const quit: Scenario = async (context) => {
	const url = context.fixtures.url("smoke.html?quit");

	// ⌘Q, Esc, then ⌘Q and Return.
	const app = await launchPane(context);
	await focusedPage(app, url);

	await app.send("key", { key: "q", modifiers: ["command"] });

	const asked = await app.waitFor(
		"Quit Pane?",
		(state) => state.quitConfirmation,
	);

	await Bun.sleep(300);
	await app.send("render", { path: join(context.out, "quit-light.png") });

	await press(app, "\u001b");

	const cancelled = await app.waitFor(
		"Quit Pane? gone, the page focused again",
		(state) =>
			!state.quitConfirmation && state.focus === "RenderWidgetHostViewCocoa",
	);

	await app.send("key", { key: "q", modifiers: ["command"] });
	await app.waitFor("Quit Pane? again", (state) => state.quitConfirmation);
	await press(app, "\r");
	const quitByReturn = await waitForExit(app.browser.pid);

	expect(quitByReturn, "Return did not quit");

	// The Dark theme, ⌘Q twice.
	const dark = await launchPane(context);
	await dark.send("theme", { theme: "dark" });
	await focusedPage(dark, url);

	await dark.send("key", { key: "q", modifiers: ["command"] });
	await dark.waitFor("Quit Pane?", (state) => state.quitConfirmation);
	await Bun.sleep(300);
	await dark.send("render", { path: join(context.out, "quit-dark.png") });

	await dark.send("key", { key: "q", modifiers: ["command"] });
	const quitByCommandQ = await waitForExit(dark.browser.pid);

	expect(quitByCommandQ, "⌘Q twice did not quit");

	return {
		asked: asked.quitConfirmation,
		escape: {
			closed: !cancelled.quitConfirmation,
			pageFocusedAgain: cancelled.focus,
		},
		quitByReturn,
		quitByCommandQ,
	};
};

export default quit;
