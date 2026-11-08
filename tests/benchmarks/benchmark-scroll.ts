import { benchmarkedApp, conditions, launchApp, stats } from "../lib/benchmark";
import type { Scenario } from "../run";

// Scroll: a steady wheel scroll down a long page of text and images
// (pages/scroll.html), counting the page's frames as its compositor reports
// them in a trace over the benchmarks' CDP connection: each BeginFrame's
// PipelineReporter says whether the frame presented every update it wanted,
// part of them, or none (dropped), and whether that hurt smoothness.
// Input.synthesizeScrollGesture scrolls: one wheel gesture of precise deltas,
// an event per vsync paced by the browser, not by this runner's socket,
// which the compositor thread scrolls as a trackpad's. Its events enter at
// the page's RenderWidgetHost: this times the page's frames and their way to
// the screen, not the app's handling of input. Each run is a launch on new
// data; each round, a scroll from the top.

const RUNS = 3;
const ROUNDS = 3;
const DISTANCE = 6_000;
const SPEED = 1_500;

type Reporter = {
	pid: number;
	state: string;
	scroll_state: string;
	affects_smoothness: boolean;
	layer_tree_host_id: number;
	frame_source: number;
	frame_sequence: number;
};

// Worst first, as COUNTS names them.
const STATES = [
	"STATE_DROPPED",
	"STATE_PRESENTED_PARTIAL",
	"STATE_PRESENTED_ALL",
];

const COUNTS = [
	"dropped",
	"partial",
	"presented",
	"affectingSmoothness",
	"onCompositorThread",
] as const;

type Round = Record<(typeof COUNTS)[number], number>;

type TraceEvent = {
	name: string;
	pid: number;
	args?: { frame_reporter?: Omit<Reporter, "pid"> };
};

// The scroll's frames. One whose main thread missed it forks and reports
// twice; it counts once, as its worse part, as cc/metrics/frame_info.cc
// merges them.
function count(reporters: Reporter[]): Round {
	const frames = Map.groupBy(
		reporters.filter((frame) => frame.scroll_state !== "SCROLL_NONE"),
		(frame) =>
			`${frame.pid} ${frame.layer_tree_host_id} ${frame.frame_source} ${frame.frame_sequence}`,
	);

	const round = Object.fromEntries(COUNTS.map((key) => [key, 0])) as Round;

	for (const parts of frames.values()) {
		const has = (test: (part: Reporter) => boolean) => Number(parts.some(test));
		const states = new Set(parts.map((part) => part.state));
		const key = COUNTS[STATES.findIndex((state) => states.has(state))];

		if (key) {
			round[key] += 1;
		}

		round.affectingSmoothness += has((part) => part.affects_smoothness);

		round.onCompositorThread += has(
			(part) => part.scroll_state === "SCROLL_COMPOSITOR_THREAD",
		);
	}

	return round;
}

const benchmarkScroll: Scenario = async (context) => {
	const started = conditions();
	const app = await benchmarkedApp(context);
	const rounds: Round[] = [];

	for (let run = 0; run < RUNS; run++) {
		const running = await launchApp(context, app, {
			url: context.fixtures.url(`scroll.html?run=${run}`),
		});

		const { page, cdp } = running;
		await page.evaluate("ready");

		const center = await page.evaluate<{ x: number; y: number }>(
			"({ x: innerWidth / 2, y: innerHeight / 2 })",
		);

		for (let round = 0; round < ROUNDS; round++) {
			await page.evaluate("scrollTo(0, 0)");
			await Bun.sleep(1_000);
			const reporters: Reporter[] = [];

			const off = cdp.on(({ method, params, sessionId }) => {
				if (method !== "Tracing.dataCollected" || sessionId) {
					return;
				}

				for (const { name, pid, args } of params.value as TraceEvent[]) {
					if (name === "PipelineReporter" && args?.frame_reporter) {
						reporters.push({ ...args.frame_reporter, pid });
					}
				}
			});

			// The reporter's category ("cc" too, which would add a hundred
			// thousand other events a round).
			await cdp.send("Tracing.start", {
				transferMode: "ReportEvents",
				traceConfig: { includedCategories: ["benchmark"] },
			});

			await cdp.send(
				"Input.synthesizeScrollGesture",
				{
					...center,
					yDistance: -DISTANCE,
					speed: SPEED,
					gestureSourceType: "mouse",
				},
				page.sessionId,
				60_000,
			);

			// Its last frames reach the screen, and report, after it ends.
			await Bun.sleep(500);

			const complete = cdp.waitFor(
				"Tracing.tracingComplete",
				undefined,
				60_000,
			);

			await cdp.send("Tracing.end");
			await complete;
			off();
			rounds.push(count(reporters));
		}

		await running.quit();
		await Bun.sleep(2_000);
	}

	return {
		runs: RUNS,
		rounds: ROUNDS,
		distance: DISTANCE,
		speed: SPEED,
		conditions: { started, ended: conditions() },
		summary: Object.fromEntries(
			COUNTS.map((key) => [key, stats(rounds.map((round) => round[key]))]),
		),
		frames: rounds,
	};
};

export default benchmarkScroll;
