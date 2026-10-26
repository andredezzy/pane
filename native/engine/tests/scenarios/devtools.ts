import { WindowWatcher } from "../lib/surfaces";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

type Target = { type: string; url: string };

// ⇧⌘I docks DevTools beside the tab, inside the app's window; again closes them.
const devtools: Scenario = async (context) => {
	const { app, has } = await launchTraced(context, "devtools");
	const watcher = new WindowWatcher(app).start();
	const profile = await app.createProfile("DevTools");
	const url = context.fixtures.url("nav-a.html?devtools");
	const tab = await app.openTab(profile, url);
	await app.page(tab, url);

	await app.send("key", { key: "i", modifiers: ["command", "shift"] });

	const open = await app.waitFor("DevTools", (state) =>
		state.devToolsTabs.includes(tab),
	);

	await Bun.sleep(1_000);
	await watcher.sample("devtools open");

	const { targetInfos } = await app.browser.cdp.send<{ targetInfos: Target[] }>(
		"Target.getTargets",
	);

	const frontend = targetInfos.find((target) =>
		target.url.startsWith("devtools://"),
	);

	await app.send("key", { key: "i", modifiers: ["command", "shift"] });

	const closed = await app.waitFor(
		"DevTools to close",
		(state) => !state.devToolsTabs.includes(tab),
	);

	expect(frontend, "no DevTools frontend target");

	expect(
		has(
			"PaneWebContentsDelegate.didChangeDevTools",
			(args) => args.open === true,
		),
		"trace lacks DevTools opening",
	);

	expect(
		has(
			"PaneWebContentsDelegate.didChangeDevTools",
			(args) => args.open === false,
		),
		"trace lacks DevTools closing",
	);

	const windows = await watcher.stop();

	expect(
		windows.others.length === 0,
		`Chrome windows appeared: ${JSON.stringify(windows.others)}`,
	);

	return {
		open: open.devToolsTabs,
		frontend: frontend?.url,
		closed: closed.devToolsTabs,
		windows,
	};
};

export default devtools;
