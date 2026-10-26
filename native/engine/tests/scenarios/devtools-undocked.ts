import { WindowWatcher } from "../lib/surfaces";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

type Target = { targetId: string; type: string; url: string };

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

	await Bun.sleep(1_500);

	// The frontend's own binding, as its "Undock into separate window" does.
	const frontend = async () => {
		const { targetInfos } = await app.browser.cdp.send<{
			targetInfos: Target[];
		}>("Target.getTargets");

		const target = targetInfos.find((info) =>
			info.url.startsWith("devtools://"),
		);

		expect(target, "no DevTools frontend");

		const { sessionId } = await app.browser.cdp.send<{ sessionId: string }>(
			"Target.attachToTarget",
			{
				targetId: target?.targetId,
				flatten: true,
			},
		);

		return (expression: string) =>
			app.browser.cdp.send("Runtime.evaluate", { expression }, sessionId);
	};

	await (await frontend())(
		"InspectorFrontendHost.setIsDocked(false, () => {})",
	);

	const undocked = await app.waitFor("the DevTools window", (state) =>
		state.nativeWindows.some((window) => window.role === "devtools"),
	);

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
