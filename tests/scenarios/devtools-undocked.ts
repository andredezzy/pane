import { poll } from "../lib/poll";
import { WindowWatcher } from "../lib/surfaces";
import { waitForTarget } from "../lib/targets";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// Undocking DevTools moves them into a window of Pane's (not Chrome's DevTools
// window); docking them again closes it.
const devtoolsUndocked: Scenario = async (context) => {
	const { app, has } = await launchTraced(context, "devtools-undocked");
	const watcher = new WindowWatcher(app).start();
	const profile = await app.createProfile("DevTools");
	const url = context.fixtures.url("nav-a.html?undock");
	const tab = await app.openTab(profile, url);
	await app.page(tab, url);

	await app.send("key", { key: "i", modifiers: ["command", "shift"] });

	await app.waitFor("docked DevTools", (state) =>
		state.devToolsTabs.includes(tab),
	);

	// The frontend's own binding, as its "Undock into separate window" does,
	// once the frontend has loaded it.
	const frontend = async () => {
		const target = await waitForTarget(
			app.browser.cdp,
			"the DevTools frontend",
			(info) => info.url.startsWith("devtools://"),
		);

		const { sessionId } = await app.browser.cdp.send<{ sessionId: string }>(
			"Target.attachToTarget",
			{
				targetId: target.targetId,
				flatten: true,
			},
		);

		const evaluate = (expression: string) =>
			app.browser.cdp.send<{ result: { value?: unknown } }>(
				"Runtime.evaluate",
				{ expression, returnByValue: true },
				sessionId,
			);

		await poll(
			"the DevTools frontend's bindings",
			async () =>
				(
					await evaluate(
						"typeof InspectorFrontendHost?.setIsDocked === 'function'",
					).catch(() => null)
				)?.result.value === true,
			15_000,
		);

		return evaluate;
	};

	await (await frontend())(
		"InspectorFrontendHost.setIsDocked(false, () => {})",
	);

	const undocked = await app.waitFor("the DevTools window", (state) =>
		state.nativeWindows.some((window) => window.role === "devtools"),
	);

	// A moment undocked, for the watcher to catch any window of Chrome's.
	await Bun.sleep(1_000);
	await watcher.sample("undocked");

	await (await frontend())("InspectorFrontendHost.setIsDocked(true, () => {})");

	const docked = await app.waitFor(
		"the DevTools window to go",
		(state) =>
			!state.nativeWindows.some((window) => window.role === "devtools"),
	);

	expect(has("DevToolsWindow.undock"), "trace lacks the undock");

	const windows = await watcher.stop();

	expect(
		windows.others.length === 0,
		`Chrome windows appeared: ${JSON.stringify(windows.others)}`,
	);

	expect(
		windows.roles.includes("devtools"),
		"no DevTools window of Pane's was seen",
	);

	return {
		undocked: undocked.nativeWindows.filter(
			(window) => window.role === "devtools",
		),
		dockedAgain: docked.devToolsTabs,
		windows,
	};
};

export default devtoolsUndocked;
