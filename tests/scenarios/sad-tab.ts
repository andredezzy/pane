import { tabOf } from "../lib/pane";
import { poll, pollValue } from "../lib/poll";
import { WindowWatcher } from "../lib/surfaces";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// The selected tab's renderer dies: the app hears of it and reloads the page,
// which comes back new and working. A page that crashes again right after that
// reload shows the app's sad tab, not Chrome's, in the page's place, with the
// keys; its Reload, or the toolbar's, pressed by a real click where it shows,
// brings it back, and the page has the keys again.
const sadTab: Scenario = async (context) => {
	const { app, trace } = await launchTraced(context, "sad-tab");
	const watcher = new WindowWatcher(app).start();
	const profile = await app.createProfile("Crash");
	const url = context.fixtures.url("crash.html?sad");
	const tab = await app.openTab(profile, url);

	const crashes = () =>
		trace().filter(
			(entry) => entry.name === "PaneWebContentsDelegate.rendererDidTerminate",
		).length;

	// Crashes the page twice in a row: the first crash reloads it, the second
	// shows the sad tab.
	const crashTwice = async () => {
		const before = crashes();

		for (let crash = before + 1; crash <= before + 2; crash++) {
			const page = await app.page(tab, url);

			expect(
				(await page.evaluate("window.beforeCrash")) === undefined,
				"the crash did not reload the page",
			);

			expect(
				(await page.evaluate<string>("document.title")) === "Crash me",
				"the page did not come back",
			);

			await page.evaluate("window.beforeCrash = true");
			// The renderer dies before it can answer, so the reply never comes.
			page.send("Page.crash").catch(() => {});

			await poll(
				`crash ${crash} to be reported`,
				() => crashes() >= crash,
				15_000,
				150,
			);
		}

		return app.waitFor("the sad tab", (state) =>
			state.crashedTabs.includes(tab),
		);
	};

	const pageIsBack = async () => {
		const back = await app.waitFor(
			"the page again",
			(state) =>
				!state.crashedTabs.includes(tab) &&
				tabOf(state, tab)?.title === "Crash me" &&
				!tabOf(state, tab)?.loading,
		);

		const page = await app.page(tab, url);

		expect(
			(await page.evaluate<string>("document.title")) === "Crash me",
			"the page did not come back",
		);

		return back;
	};

	// The sad tab shows in the page's place, and takes the keys from the page
	// that died.
	const shown = async () => {
		const sad = await app.waitFor(
			"the sad tab with the keys",
			(state) => state.focus === "pane.sadTab",
		);

		// Laid out at its next frame, after the keys have gone to it.
		const frames = await pollValue(
			() => app.frames("pane.sadTab"),
			(each) => (each["pane.sadTab.reload"]?.[2] ?? 0) > 0,
			5_000,
		);

		expect(
			(frames["pane.sadTab.reload"]?.[2] ?? 0) > 0,
			`the sad tab's Reload does not show: ${JSON.stringify(frames)}`,
		);

		return sad;
	};

	await crashTwice();
	await shown();
	await watcher.sample();

	// The sad tab's own Reload.
	await app.click("pane.sadTab.reload");
	const back = await pageIsBack();

	expect(
		back.focus === "pane.content",
		`back from the sad tab, ${back.focus} had the keys`,
	);

	// The toolbar's Reload (and ⌘R) bring a sad tab back too.
	await crashTwice();
	await shown();
	await app.click("pane.toolbar.reload");
	await pageIsBack();

	await watcher.stop();
};

export default sadTab;
