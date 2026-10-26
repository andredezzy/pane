import { WindowWatcher } from "../lib/surfaces";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// A page's fullscreen fills the app's window (no sidebar or toolbar), the
// app shows the Esc hint, and Esc leaves it.
const fullscreen: Scenario = async (context) => {
	const { app, has } = await launchTraced(context, "fullscreen");
	const watcher = new WindowWatcher(app).start();
	const profile = await app.createProfile("Fullscreen");
	const url = context.fixtures.url("fullscreen.html");
	const tab = await app.openTab(profile, url);
	const page = await app.page(tab, url);

	await page.send("Runtime.evaluate", {
		expression: "document.documentElement.requestFullscreen()",
		userGesture: true,
		awaitPromise: true,
	});

	const entered = await app.waitFor(
		"page fullscreen",
		(state) => state.pageFullscreen,
	);

	const element = await page.evaluate<string | null>(
		"document.fullscreenElement?.tagName ?? null",
	);

	await watcher.sample("fullscreen");

	await app.send("key", { key: "\u001b" });

	const left = await app.waitFor(
		"fullscreen to end",
		(state) => !state.pageFullscreen,
	);

	const after = await page.evaluate<string | null>(
		"document.fullscreenElement?.tagName ?? null",
	);

	expect(element === "HTML", `fullscreen element ${element}`);

	expect(
		entered.exclusiveAccessHint?.includes("Esc"),
		`hint ${entered.exclusiveAccessHint}`,
	);

	expect(after === null, "Esc did not leave fullscreen");

	expect(
		has("PaneBrowserWindowDelegate.showExclusiveAccessHint"),
		"trace lacks the hint",
	);

	const windows = await watcher.stop();

	expect(
		windows.others.length === 0,
		`Chrome windows appeared: ${JSON.stringify(windows.others)}`,
	);

	return {
		entered: {
			pageFullscreen: entered.pageFullscreen,
			hint: entered.exclusiveAccessHint,
			element,
		},
		left: { pageFullscreen: left.pageFullscreen, element: after },
		windows,
	};
};

export default fullscreen;
