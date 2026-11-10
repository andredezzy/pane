import { benchmarkedApp, conditions, launchApp } from "../lib/benchmark";
import type { Scenario } from "../run";

// A painted frame's way to the screen (docs/plans/views-migration.md, the
// gate): benchmark-scroll's scroll, and for each of the page's frames in it
// how long cc's PipelineReporter says it took from its compositor's
// submission to its presentation (SubmitCompositorFrameToPresentation-
// CompositorFrame), with the stages under it, in a trace of the "benchmark"
// category over the benchmarks' CDP connection. The page's frames are its
// renderer's, those that scrolled on the compositor thread; the browser's are
// the window's own. Each run is a launch on new data; each round, a scroll
// from the top.

const RUNS = 3;
const ROUNDS = 3;
const DISTANCE = 6_000;
const SPEED = 1_500;

const STAGES = [
	"SubmitCompositorFrameToPresentationCompositorFrame",
	"SubmitToReceiveCompositorFrame",
	"ReceiveCompositorFrameToStartDraw",
	"StartDrawToSwapStart",
	"SwapStartToBufferAvailable",
	"BufferAvailableToBufferReady",
	"BufferReadyToLatch",
	"LatchToSwapEnd",
	"SwapEndToPresentationCompositorFrame",
];

type TraceEvent = {
	name: string;
	ph: string;
	pid: number;
	ts: number;
	id?: string;
	id2?: { local?: string; global?: string };
	args?: { frame_reporter?: { scroll_state?: string } };
};

const round = (value: number) => Math.round(value * 100) / 100;

function percentile(values: number[], share: number): number {
	const sorted = [...values].sort((a, b) => a - b);

	return sorted[Math.floor(sorted.length * share)] ?? 0;
}

const benchmarkFrameLatency: Scenario = async (context) => {
	const started = conditions();
	const app = await benchmarkedApp(context);

	const durations: Record<string, number[]> = Object.fromEntries(
		STAGES.map((stage) => [stage, []]),
	);

	for (let run = 0; run < RUNS; run++) {
		const running = await launchApp(context, app, {
			url: context.fixtures.url(`scroll.html?latency=${run}`),
		});

		const { page, cdp } = running;
		await page.evaluate("ready");

		const center = await page.evaluate<{ x: number; y: number }>(
			"({ x: innerWidth / 2, y: innerHeight / 2 })",
		);

		for (let scroll = 0; scroll < ROUNDS; scroll++) {
			await page.evaluate("scrollTo(0, 0)");
			await Bun.sleep(1_000);

			const events: TraceEvent[] = [];

			const off = cdp.on(({ method, params, sessionId }) => {
				if (method !== "Tracing.dataCollected" || sessionId) {
					return;
				}

				for (const event of params.value as TraceEvent[]) {
					if (
						event.name === "PipelineReporter" ||
						STAGES.includes(event.name)
					) {
						events.push(event);
					}
				}
			});

			try {
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
			} finally {
				off();
			}

			const pages = new Set(
				events
					.filter(
						(event) =>
							event.name === "PipelineReporter" &&
							event.args?.frame_reporter?.scroll_state ===
								"SCROLL_COMPOSITOR_THREAD",
					)
					.map((event) => event.pid),
			);

			// Each stage is an async slice, its begin and end paired by id.
			const open = new Map<string, number>();

			for (const event of events.sort((a, b) => a.ts - b.ts)) {
				if (!STAGES.includes(event.name) || !pages.has(event.pid)) {
					continue;
				}

				const id = event.id2?.local ?? event.id2?.global ?? event.id ?? "";
				const key = `${event.pid} ${id} ${event.name}`;
				const begin = open.get(key);

				if (event.ph === "b") {
					open.set(key, event.ts);
				} else if (event.ph === "e" && begin !== undefined) {
					durations[event.name]?.push((event.ts - begin) / 1_000);
					open.delete(key);
				}
			}
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
			STAGES.map((stage) => {
				const values = durations[stage] ?? [];

				return [
					stage,
					{
						frames: values.length,
						medianMs: round(percentile(values, 0.5)),
						p90Ms: round(percentile(values, 0.9)),
					},
				];
			}),
		),
	};
};

export default benchmarkFrameLatency;
