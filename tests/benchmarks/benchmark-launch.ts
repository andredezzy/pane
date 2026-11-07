import {
	benchmarkedApp,
	conditions,
	type LaunchTimes,
	launchApp,
	stats,
} from "../lib/benchmark";
import type { Scenario } from "../run";

// Launch, timed from the exec: the first window on screen and the first tab's
// page loaded. Cold is a first launch on new data; warm, the
// relaunch on the data the cold one left. Neither is cold for the OS: the
// app's files stay in its cache from one launch to the next, and clearing
// that cache (purge) needs root.

const RUNS = 7;

// What the app gets time to write before it quits, as after a first launch.
const SETTLE_MS = 3_000;

function summary(launches: LaunchTimes[]) {
	const of = (pick: (times: LaunchTimes) => number | null) => {
		const values = launches
			.map(pick)
			.filter((value): value is number => typeof value === "number");

		return values.length > 0 ? stats(values) : null;
	};

	return {
		window: of((times) => times.window),
		devTools: of((times) => times.devTools),
		pageFirstContentfulPaint: of((times) => times.page.firstContentfulPaint),
		pageLoaded: of((times) => times.page.loaded),
	};
}

const benchmarkLaunch: Scenario = async (context) => {
	const started = conditions();
	const app = await benchmarkedApp(context);
	const cold: LaunchTimes[] = [];
	const warm: LaunchTimes[] = [];

	for (let run = 0; run < RUNS; run++) {
		const first = await launchApp(context, app, {
			url: context.fixtures.url(`smoke.html?launch=cold-${run}`),
		});

		await Bun.sleep(SETTLE_MS);
		await first.quit();
		await Bun.sleep(2_000);

		const again = await launchApp(context, app, {
			dataDir: first.dataDir,
			url: context.fixtures.url(`smoke.html?launch=warm-${run}`),
		});

		await Bun.sleep(SETTLE_MS);
		await again.quit();
		await Bun.sleep(2_000);

		cold.push(first.times);
		warm.push(again.times);
	}

	return {
		runs: RUNS,
		conditions: { started, ended: conditions() },
		summary: { cold: summary(cold), warm: summary(warm) },
		launches: { cold, warm },
	};
};

export default benchmarkLaunch;
