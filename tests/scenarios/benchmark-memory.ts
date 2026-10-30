import {
	benchmarkedApps,
	conditions,
	inTurn,
	launchApp,
	type MemorySnapshot,
	memoryOf,
	stats,
} from "../lib/benchmark";
import type { Scenario } from "../run";

// Memory, the Electron app against the native one: every process of the app
// (the browser, its helpers, renderers, GPU, utilities, Crashpad), each one's
// physical footprint summed, with one tab, five, and ten. The tabs are the
// fixtures' small local pages, so what grows with them is the browser's and
// the app's own cost per tab, not a site's. Each count is read once the app
// has been idle for a while after its last tab loaded; each run is a launch
// on new data.

const RUNS = 3;
const IDLE_MS = 30_000;
const TAB_COUNTS = [1, 5, 10];
const PAGES = ["smoke.html", "find.html", "nav-a.html"];

const benchmarkMemory: Scenario = async (context) => {
	const started = conditions();
	const apps = await benchmarkedApps(context);
	const scratch = context.tempDir("pane-benchmark-footprint-");

	const page = (tab: number) =>
		context.fixtures.url(`${PAGES[(tab - 1) % PAGES.length]}?tab=${tab}`);

	const snapshots: Record<
		string,
		Record<number, MemorySnapshot[]>
	> = Object.fromEntries(
		apps.map((app) => [
			app.kind,
			Object.fromEntries(TAB_COUNTS.map((count) => [count, []])),
		]),
	);

	for (let run = 0; run < RUNS; run++) {
		for (const app of inTurn(apps, run)) {
			const running = await launchApp(context, app, {
				url: page(1),
				automation: true,
			});

			let open = 1;

			for (const count of TAB_COUNTS) {
				while (open < count) {
					open += 1;
					await running.openTab(page(open));
				}

				await Bun.sleep(IDLE_MS);
				snapshots[app.kind]?.[count]?.push(await memoryOf(app, scratch));
			}

			await running.quit();
			await Bun.sleep(2_000);
		}
	}

	const summary = Object.fromEntries(
		Object.entries(snapshots).map(([kind, byCount]) => [
			kind,
			Object.fromEntries(
				Object.entries(byCount).map(([count, runs]) => [
					count,
					{
						footprintSumBytes: stats(runs.map((run) => run.footprintSumBytes)),
						footprintTotalBytes: stats(
							runs.map((run) => run.footprintTotalBytes),
						),
						rssSumBytes: stats(runs.map((run) => run.rssSumBytes)),
						processes: stats(runs.map((run) => run.processes)),
					},
				]),
			),
		]),
	);

	return {
		runs: RUNS,
		idleMs: IDLE_MS,
		pages: PAGES,
		conditions: { started, ended: conditions() },
		summary,
		snapshots,
	};
};

export default benchmarkMemory;
