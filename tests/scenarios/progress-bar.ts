import { launchPane, tabOf } from "../lib/pane";
import { expect, type Scenario } from "../run";

// The address bar's progress follows the app's tabs and the engine's loads:
// the rules of the bar are ToolbarProgressTests'. A load shows at once, fading
// in. A tab come forward with no load hides the bar at once, and one mid-load
// shows its own progress. A finished load fills the bar and fades it out.
const progressBar: Scenario = async (context) => {
	const app = await launchPane(context);
	const profile = await app.createProfile("Progress");

	const { tab: idle } = await app.openPage(
		profile,
		context.fixtures.url("nav-a.html"),
	);

	const { tab: busy } = await app.openPage(
		profile,
		context.fixtures.url("light.html"),
	);

	// A navigation the server holds 3 s: the bar shows at once, at its start.
	const slowURL = context.fixtures.url("nav-b.html?delay=3000");
	await app.send("navigate", { input: slowURL });

	const started = await app.waitFor(
		"the bar started",
		(state) => state.progressBar.started,
		1_000,
	);

	expect(
		started.progressBar.progress >= 0.05,
		`the bar started at ${started.progressBar.progress}`,
	);

	const shown = await app.waitFor(
		"the bar faded in",
		(state) => state.progressBar.alpha === 1,
		1_000,
	);

	// Another tab, with no load: the bar hides at once.
	const away = (await app.send("select", { tab: idle })).state;

	expect(
		away.progressBar.alpha === 0 && !away.progressBar.started,
		`a tab with no load showed the bar: ${JSON.stringify(away.progressBar)}`,
	);

	// Back to the loading tab: its own progress, at once.
	const back = (await app.send("select", { tab: busy })).state;

	expect(
		back.progressBar.started &&
			back.progressBar.progress === shown.progressBar.progress,
		`back on the loading tab: ${JSON.stringify(back.progressBar)}`,
	);

	// The load ends: the bar fills, then fades out.
	await app.waitFor(
		"the bar full",
		(state) =>
			tabOf(state, busy)?.loading === false && state.progressBar.progress === 1,
		6_000,
	);

	await app.waitFor(
		"the bar gone after the load",
		(state) => state.progressBar.alpha === 0,
		2_000,
	);
};

export default progressBar;
