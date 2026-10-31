import { bringToFront, launchPane } from "../lib/pane";
import { poll } from "../lib/poll";
import { expect, type Scenario } from "../run";

// The AppleScript dictionary Pane keeps from Chromium (ADR 0112) answers for
// Pane's window, as launchers and clipboard tools ask a browser: one window,
// whose active tab, its URL and title, is the tab Pane shows, in whichever
// profile. Its tabs are the shown profile's open ones, Chromium's Browser
// for it, not the sidebar's every tab. Run in the app against itself
// (current application, the dictionary's raw codes), so no Apple Events
// permission is asked of the Mac's user.

const WINDOWS = "count every «class cwin»";
const URL_OF_ACTIVE = "get «class URL » of «class acTa» of «class cwin» 1";
const TITLE_OF_ACTIVE = "get «class pnam» of «class acTa» of «class cwin» 1";
const TABS = "count every «class CrTb» of «class cwin» 1";

const applescript: Scenario = async (context) => {
	const app = await launchPane(context);
	const work = await app.createProfile("Work");
	const personal = await app.createProfile("Personal", "ROSE");

	const url = (name: string) =>
		context.fixtures.url(`switcher.html?name=${name}`);

	const tabs: Record<string, string> = {};

	for (const [profile, name] of [
		[work, "Alpha"],
		[work, "Bravo"],
		[personal, "Charlie"],
	] as const) {
		tabs[name] = await app.openTab(profile, url(name));
		await app.page(tabs[name], url(name));
	}

	const run = async (script: string) =>
		(
			await app.send<string | null>("appleScript", {
				source: `tell current application to ${script}`,
			})
		).result;

	const answers: Record<string, unknown> = {};

	// Chromium's front window is the Browser it activated last, and the
	// engine activates the shown profile's only with Pane's window key
	// (WindowHost::SetActiveProfile), as Chrome activates a window: shown in
	// the background, another profile is the front window once Pane is in
	// front (ADR 0112). The switch to Personal is checked with Pane in
	// front, and deferred without --foreground; Work's tabs, the Browser in
	// front either way, are checked always.
	const front = await bringToFront(
		context,
		app,
		"the front window after a switch to another profile",
	);

	const shown = front
		? ([
				["Bravo", "2"],
				["Charlie", "1"],
				["Alpha", "2"],
			] as const)
		: ([
				["Bravo", "2"],
				["Alpha", "2"],
			] as const);

	for (const [name, tabCount] of shown) {
		await app.send("select", { tab: tabs[name] });

		await app.waitFor(
			`${name} selected`,
			(state) => state.activeTabID === tabs[name],
		);

		// Activated, the Browser takes a turn to be the front window
		// (HostedBrowserWindow::Activate posts it): the answer is read once
		// it names the tab, within a second, and how long that took is kept.
		const asked = Date.now();

		await poll(
			`AppleScript naming ${name}`,
			async () => (await run(URL_OF_ACTIVE)) === url(name),
			1_000,
			20,
		).catch(() => undefined);

		const namedAfterMs = Date.now() - asked;

		const answer = {
			windows: await run(WINDOWS),
			url: await run(URL_OF_ACTIVE),
			title: await run(TITLE_OF_ACTIVE),
			tabs: await run(TABS),
		};

		expect(
			answer.windows === "1" &&
				answer.url === url(name) &&
				answer.title === name &&
				answer.tabs === tabCount,
			`with ${name} shown, AppleScript answers ${JSON.stringify(answer)}`,
		);

		answers[name] = { ...answer, namedAfterMs };
	}

	return answers;
};

export default applescript;
