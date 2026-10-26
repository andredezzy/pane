import { clickButton, isScreenLocked, mainWindowOf } from "../lib/cua";
import { tabOf } from "../lib/pane";
import { WindowWatcher } from "../lib/surfaces";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// A page that crashes again right after its automatic reload shows the
// app's sad tab, not Chrome's; Reload brings it back.
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

	for (let crash = 1; crash <= 2; crash++) {
		const page = await app.page(tab, url);
		page.send("Page.crash").catch(() => {});

		const deadline = Date.now() + 15_000;

		while (crashes() < crash && Date.now() < deadline) {
			await Bun.sleep(150);
		}

		expect(crashes() >= crash, `crash ${crash} was not reported`);
	}

	const sad = await app.waitFor("the sad tab", (state) =>
		state.crashedTabs.includes(tab),
	);

	await watcher.sample("sad tab");

	let via = "automation";

	if (await isScreenLocked()) {
		await app.send("reloadCrashed", { tab });
	} else {
		await clickButton(
			app.browser.pid,
			await mainWindowOf(app.browser.pid),
			"Reload",
		);

		via = "cua";
	}

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
