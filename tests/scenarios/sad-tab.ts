import { clickButton, isScreenLocked, mainWindowOf } from "../lib/cua";
import { tabOf } from "../lib/pane";
import { WindowWatcher } from "../lib/surfaces";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// A page that crashes again right after its automatic reload shows the
// app's sad tab, not Chrome's; its Reload, or the toolbar's, brings it back.
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
			page.send("Page.crash").catch(() => {});

			const deadline = Date.now() + 15_000;

			while (crashes() < crash && Date.now() < deadline) {
				await Bun.sleep(150);
			}

			expect(crashes() >= crash, `crash ${crash} was not reported`);
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

	const sad = await crashTwice();

	await watcher.sample("sad tab");

	const locked = await isScreenLocked();
	const via = locked ? "automation" : "cua";

	// The sad tab's own Reload.
	if (locked) {
		await app.send("reloadCrashed", { tab });
	} else {
		await clickButton(
			app.browser.pid,
			await mainWindowOf(app.browser.pid),
			"Reload",
			"pane.sadTab.reload",
		);
	}

	const back = await pageIsBack();

	// The toolbar's Reload (and ⌘R) bring a sad tab back too.
	await crashTwice();

	if (locked) {
		await app.send("reload");
	} else {
		await clickButton(
			app.browser.pid,
			await mainWindowOf(app.browser.pid),
			"Reload",
			"pane.toolbar.reload",
		);
	}

	await pageIsBack();

	const windows = await watcher.stop();

	expect(
		windows.others.length === 0,
		`Chrome windows appeared: ${JSON.stringify(windows.others)}`,
	);

	return {
		crashes: crashes(),
		crashedTabs: sad.crashedTabs,
		reloadVia: via,
		tab: tabOf(back, tab),
		windows,
	};
};

export default sadTab;
