import { join } from "node:path";
import { pathToFileURL } from "node:url";
import { handURLs } from "../lib/browser";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// What another app hands Pane opens in Pane's window, as in Chrome: a link
// clicked in Mail with Pane as the default browser, a file opened with it.
// The system hands the URLs to the running app (handURLs: on the Mac Launch
// Services, a page and a file in an Apple Event each, without activating
// it; on Windows a launch with them, which the running copy's process
// singleton takes); the engine passes them to the app rather than to a
// window of Chrome's, and each opens as a new tab in the profile in use, the
// last one selected and loaded.
const openURLs: Scenario = async (context) => {
	const { app, trace, waitForEntry } = await launchTraced(context, "open-urls");

	await app.createProfile("Work");
	const personal = await app.createProfile("Personal", "ROSE");
	await app.openTab(personal, context.fixtures.url("light.html?open-urls"));

	const before = await app.state();

	expect(
		before.activeProfileID === personal,
		`the profile in use is ${before.activeProfileID}, not Personal`,
	);

	const link = context.fixtures.url("nav-a.html?handed-over");

	const file = pathToFileURL(
		join(import.meta.dir, "..", "pages", "nav-b.html"),
	).href;

	const failed = await handURLs(
		{ binary: context.binary, pid: app.browser.pid, dataDir: app.dataDir },
		context.tempDir("pane-open-urls-home-"),
		[link, file],
	);

	expect(failed === null, `${failed}`);

	const opened = await app.waitFor(
		"both URLs open in Personal, the last one selected",
		(state) => {
			const tabs =
				state.profiles.find((profile) => profile.id === personal)?.tabs ?? [];

			const last = tabs.slice(-2);

			return (
				last.some((tab) => tab.url === link && tab.loaded) &&
				last.some((tab) => tab.url === file && tab.loaded) &&
				state.activeTabID === tabs.at(-1)?.id
			);
		},
		30_000,
	);

	await waitForEntry("PaneBrowserApplicationDelegate.openURLs");

	const passed = trace()
		.filter((each) => each.name === "PaneBrowserApplicationDelegate.openURLs")
		.reduce((sum, each) => sum + Number(each.args.count), 0);

	expect(passed === 2, `the engine passed ${passed} URLs`);

	const others = opened.profiles
		.filter((profile) => profile.id !== personal)
		.flatMap((profile) => profile.tabs);

	expect(
		others.length === 0,
		`a URL opened in another profile: ${JSON.stringify(others)}`,
	);

	// No window of Chrome's opened them.
	expect(
		opened.nativeWindows.every((window) => window.role === "main"),
		`a window showed with the main one: ${JSON.stringify(opened.nativeWindows)}`,
	);
};

export default openURLs;
