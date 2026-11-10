import { tabOf } from "../lib/pane";
import { answerSheet, WindowWatcher, waitForSheet } from "../lib/surfaces";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// A page stuck in a loop that ignores input gets the app's "Page
// Unresponsive" sheet. Wait keeps the page, and the sheet comes back while
// it stays stuck; Exit Page ends it, and the tab reloads.
const hungRenderer: Scenario = async (context) => {
	const { app, has, waitForEntry } = await launchTraced(
		context,
		"hung-renderer",
	);

	const watcher = new WindowWatcher(app).start();
	const profile = await app.createProfile("Hang");
	const url = context.fixtures.url("hang.html?auto");
	const tab = await app.openTab(profile, url);

	// No CDP on this page: Chromium ignores an unresponsive page while a
	// debugger is attached. The page hangs itself a second after loading,
	// renaming itself first; then a key typed into it waits for an answer
	// that never comes.
	await app.waitFor(
		"the page hung",
		(state) => tabOf(state, tab)?.title === "Hanging",
	);

	// The page loops 50 ms after renaming itself, which nothing outside it
	// can see: a key sooner is answered before the loop, and nothing hangs.
	await Bun.sleep(250);

	await app.send("typeInPage", { key: "a" });

	const sheet = await waitForSheet(app, "unresponsive", 60_000);

	expect(
		sheet.buttons?.join(",") === "Wait,Exit Page",
		`buttons ${sheet.buttons}`,
	);

	// Wait: the sheet goes and the renderer lives. The key is still
	// unanswered, so the hang monitor asks again.
	await answerSheet(app, "Wait");

	await app.waitFor(
		"the sheet gone after Wait",
		(state) => state.sheet?.kind !== "unresponsive",
	);

	expect(
		!has("PaneWebContentsDelegate.rendererDidTerminate"),
		"Wait ended the renderer",
	);

	await waitForSheet(app, "unresponsive", 60_000);

	expect(
		!has("PaneWebContentsDelegate.rendererDidTerminate"),
		"the renderer ended while the page was waited for",
	);

	await answerSheet(app, "Exit Page");

	const terminated = await waitForEntry(
		"PaneWebContentsDelegate.rendererDidTerminate",
		() => true,
		15_000,
	).catch(() => null);

	expect(terminated, "Exit Page did not end the renderer");

	// Loaded again: it hangs itself again a second later.
	await app.waitFor(
		"the reloaded page",
		(state) =>
			["Hang", "Hanging"].includes(tabOf(state, tab)?.title ?? "") &&
			!tabOf(state, tab)?.loading,
	);

	await watcher.stop();
};

export default hungRenderer;
