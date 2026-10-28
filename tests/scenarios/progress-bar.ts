import { launchPane, tabOf } from "../lib/pane";
import { expect, type Scenario } from "../run";

// The address bar's progress behaves as Chrome's toolbar's (the user's call:
// exactly as a stable browser's; ToolbarProgress, from Chromium's
// ToolbarProgressBar). A load shows at once at its real progress, at least 5%,
// fading in. A tab come forward with no load hides the bar at once; one
// mid-load shows its own progress. Five quiet seconds start the slow mode's
// shimmer. A finished load fills the bar, holds 100 ms, and fades out.
const progressBar: Scenario = async (context) => {
	const app = await launchPane(context);
	const profile = await app.createProfile("Progress");
	const idleURL = context.fixtures.url("nav-a.html");
	const idle = await app.openTab(profile, idleURL);

	await app.waitFor(
		"the first tab loaded",
		(state) =>
			tabOf(state, idle)?.url === idleURL &&
			tabOf(state, idle)?.loading === false,
	);

	const busyURL = context.fixtures.url("light.html");
	const busy = await app.openTab(profile, busyURL);

	await app.waitFor(
		"the second tab loaded",
		(state) =>
			tabOf(state, busy)?.url === busyURL &&
			tabOf(state, busy)?.loading === false,
	);

	// A finished load: hidden.
	const rest = await app.waitFor(
		"the bar gone",
		(state) => state.progressBar.alpha === 0,
	);

	expect(!rest.progressBar.started, "the bar stayed started after a load");

	// A navigation the server holds 9 s: the bar shows at once, at its start.
	const slowURL = context.fixtures.url("nav-b.html?delay=9000");
	const navigated = Date.now();
	await app.send("navigate", { input: slowURL });

	const started = await app.waitFor(
		"the bar started",
		(state) => state.progressBar.started,
		1_000,
	);

	const startedMs = Date.now() - navigated;

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

	// Five seconds with no news: the slow mode's shimmer.
	const backAt = Date.now();

	const slow = await app.waitFor(
		"the slow mode",
		(state) => state.progressBar.shimmer,
		8_000,
	);

	const slowMs = Date.now() - backAt;

	expect(
		slowMs >= 4_800,
		`the slow mode began ${slowMs} ms after the last news, before 5 s`,
	);

	// The load ends: the bar fills, then fades out.
	const full = await app.waitFor(
		"the bar full",
		(state) =>
			tabOf(state, busy)?.loading === false && state.progressBar.progress === 1,
		12_000,
	);

	const gone = await app.waitFor(
		"the bar gone after the load",
		(state) => state.progressBar.alpha === 0,
		2_000,
	);

	return {
		startedMs,
		started: started.progressBar,
		away: away.progressBar,
		back: back.progressBar,
		slowMs,
		slow: slow.progressBar,
		full: full.progressBar,
		gone: gone.progressBar,
	};
};

export default progressBar;
