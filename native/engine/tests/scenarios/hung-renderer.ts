import { tabOf } from "../lib/pane";
import { answerSheet, WindowWatcher, waitForSheet } from "../lib/surfaces";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// A page stuck in a loop that ignores input gets the app's "Page
// Unresponsive" sheet; Exit Page ends it, and the tab reloads.
const hungRenderer: Scenario = async (context) => {
	const { app, has } = await launchTraced(context, "hung-renderer");
	const watcher = new WindowWatcher(app).start();
	const profile = await app.createProfile("Hang");
	const url = context.fixtures.url("hang.html?auto");
	const tab = await app.openTab(profile, url);

	// No CDP on this page: Chromium ignores an unresponsive page while a
	// debugger is attached. The page hangs itself a second after loading;
	// then a key typed into it waits for an answer that never comes.
	await app.waitFor(
		"the page",
		(state) =>
			tabOf(state, tab)?.title === "Hang" && !tabOf(state, tab)?.loading,
	);

	await Bun.sleep(2_000);
	await app.send("typeInPage", { key: "a" });

	const sheet = await waitForSheet(app, "unresponsive", 60_000);

	expect(
		sheet.buttons?.join(",") === "Wait,Exit Page",
		`buttons ${sheet.buttons}`,
	);

	const answer = await answerSheet(app, "Exit Page");

	const terminated = await (async () => {
		const deadline = Date.now() + 15_000;

		while (Date.now() < deadline) {
			if (has("PaneWebContentsDelegate.rendererDidTerminate")) {
				return true;
			}

			await Bun.sleep(200);
		}

		return false;
	})();

	expect(terminated, "Exit Page did not end the renderer");

	const back = await app.waitFor(
		"the reloaded page",
		(state) =>
			tabOf(state, tab)?.title === "Hang" && !tabOf(state, tab)?.loading,
	);

	expect(
		has("PaneWebContentsDelegate.becameUnresponsive"),
		"trace lacks becameUnresponsive",
	);

	expect(has("PaneUnresponsivePage.terminate"), "trace lacks the answer");

	const windows = await watcher.stop();

	expect(
		windows.others.length === 0,
		`Chrome windows appeared: ${JSON.stringify(windows.others)}`,
	);

	return { sheet, answer, tab: tabOf(back, tab), windows };
};

export default hungRenderer;
