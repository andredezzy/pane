import { launchPane } from "../lib/pane";
import { WindowWatcher } from "../lib/surfaces";
import { waitForTarget } from "../lib/targets";
import { expect, type Scenario } from "../run";

// ⇧⌘I docks DevTools beside the tab, inside the app's window; again closes them.
const devtools: Scenario = async (context) => {
	const app = await launchPane(context);
	const watcher = new WindowWatcher(app).start();
	const profile = await app.createProfile("DevTools");
	const url = context.fixtures.url("nav-a.html?devtools");
	const { tab } = await app.openPage(profile, url);

	await app.send("key", { key: "i", modifiers: ["command", "shift"] });

	await app.waitFor("DevTools", (state) => state.devToolsTabs.includes(tab));

	const frontend = await waitForTarget(
		app.browser.cdp,
		"the DevTools frontend",
		(target) => target.url.startsWith("devtools://"),
	).catch(() => null);

	// A moment open, for the watcher to catch any window of Chrome's.
	await Bun.sleep(1_000);
	await watcher.sample();

	await app.send("key", { key: "i", modifiers: ["command", "shift"] });

	await app.waitFor(
		"DevTools to close",
		(state) => !state.devToolsTabs.includes(tab),
	);

	expect(frontend, "no DevTools frontend target");

	await watcher.stop();
};

export default devtools;
