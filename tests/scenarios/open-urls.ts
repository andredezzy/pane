import { dirname, join } from "node:path";
import { pathToFileURL } from "node:url";
import { swiftTool } from "../lib/tools";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// What another app hands Pane opens in Pane's window, as in Chrome: a link
// clicked in Mail with Pane as the default browser, a file opened with it.
// Launch Services sends the URLs to the running app (tools/open-urls.swift,
// without activating it), a page and a file in an Apple Event each; the
// engine passes them to the app rather than to a window of Chrome's, and
// each opens as a new tab in the profile in use, the last one selected and
// loaded.
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

	const tool = await swiftTool("open-urls");
	const bundle = dirname(dirname(dirname(context.binary)));
	const home = context.tempDir("pane-open-urls-home-");

	const handed =
		await Bun.$`${tool} ${bundle} ${app.browser.pid} ${home} ${link} ${file}`
			.quiet()
			.nothrow();

	expect(
		handed.exitCode === 0,
		`open-urls failed: ${handed.stderr.toString().trim()}`,
	);

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

	return {
		handedTo: JSON.parse(handed.stdout.toString()),
		tabs: opened.profiles
			.find((profile) => profile.id === personal)
			?.tabs.map((tab) => tab.url),
	};
};

export default openURLs;
