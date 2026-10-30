import {
	benchmarkedApps,
	conditions,
	inTurn,
	type LaunchTimes,
	launchApp,
	stats,
} from "../lib/benchmark";
import type { Scenario } from "../run";

// Launch, the Electron app against the native one, timed from the exec: the
// first window on screen, the Electron app's UI (a page of its own), and the
// first tab's page loaded. Cold is a first launch on new data; warm, the
// relaunch on the data the cold one left. Neither is cold for the OS: the
// app's files stay in its cache from one launch to the next, and clearing
// that cache (purge) needs root.

const RUNS = 7;

// What the app gets time to write before it quits, as after a first launch.
const SETTLE_MS = 3_000;

type Launches = Record<"cold" | "warm", LaunchTimes[]>;

function summary(launches: LaunchTimes[]) {
	const of = (pick: (times: LaunchTimes) => number | null | undefined) => {
		const values = launches
			.map(pick)
			.filter((value): value is number => typeof value === "number");

		return values.length > 0 ? stats(values) : null;
	};

	return {
		window: of((times) => times.window),
		devTools: of((times) => times.devTools),
		uiFirstContentfulPaint: of((times) => times.ui?.firstContentfulPaint),
		uiLoaded: of((times) => times.ui?.loaded),
		uiProfileShown: of((times) => times.ui?.profileShown),
		tabRequested: of((times) => times.tabRequested),
		pageFirstContentfulPaint: of((times) => times.page.firstContentfulPaint),
		pageLoaded: of((times) => times.page.loaded),
	};
}

const benchmarkLaunch: Scenario = async (context) => {
	const started = conditions();
	const apps = await benchmarkedApps(context);

	const launches: Record<string, Launches> = Object.fromEntries(
		apps.map((app) => [app.kind, { cold: [], warm: [] }]),
	);

	for (let run = 0; run < RUNS; run++) {
		for (const app of inTurn(apps, run)) {
			const cold = await launchApp(context, app, {
				url: context.fixtures.url(`smoke.html?launch=cold-${run}`),
			});

			await Bun.sleep(SETTLE_MS);
			await cold.quit();
			await Bun.sleep(2_000);

			const warm = await launchApp(context, app, {
				dataDir: cold.dataDir,
				url: context.fixtures.url(`smoke.html?launch=warm-${run}`),
			});

			await Bun.sleep(SETTLE_MS);
			await warm.quit();
			await Bun.sleep(2_000);

			launches[app.kind]?.cold.push(cold.times);
			launches[app.kind]?.warm.push(warm.times);
		}
	}

	return {
		runs: RUNS,
		conditions: { started, ended: conditions() },
		summary: Object.fromEntries(
			Object.entries(launches).map(([kind, { cold, warm }]) => [
				kind,
				{ cold: summary(cold), warm: summary(warm) },
			]),
		),
		launches,
	};
};

export default benchmarkLaunch;
