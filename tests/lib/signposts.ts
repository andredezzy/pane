// The app's signposts, Instruments' Points of Interest (Log.interaction, and
// the Views build's pane/ui/interaction_log.h): each interval from its input
// to the frame it answers with, read from the unified log while `run` drives
// the app. /usr/bin/log by its path: zsh has a builtin of the same name.
// Windows' trace (pane/ui/interaction_log_win.cc) is W6's.
import type { Subprocess } from "bun";
import { bySystem, noWindowsSide } from "./platform";

type Signpost = {
	eventType?: string;
	signpostType?: "begin" | "end" | "event";
	signpostName?: string;
	signpostID?: number;
	processID?: number;
	timestamp?: string;
	eventMessage?: string;
};

// The log's "2026-10-30 15:41:02.123456-0300", in milliseconds.
function millisecondsOf(timestamp: string): number {
	const [date, time] = timestamp.split(" ");
	const [clock = "", fraction = "0"] = (time ?? "").slice(0, 15).split(".");

	return Date.parse(`${date}T${clock}Z`) + Number(`0.${fraction}`) * 1000;
}

// One interval, from its input, in milliseconds: to the frame that answers
// it, and, where the build says, to when that frame went out.
export type FrameInterval = {
	// The Views build ends an interval when the UI thread hears its frame
	// reached the screen, and says how long before that was (late=, in
	// microseconds), when the GPU process's prediction of the screen is
	// already past: the interval ends at the earlier of the two.
	ms: number;
	// To the frame's commit to Core Animation, the frame handed to the
	// window server (commit=), where the Swift app's intervals end.
	committedMs?: number;
	// To the screen as the GPU process predicts it, ahead or past (glass=).
	presentedMs?: number;
};

// The unified log's stream of Pane's signposts, one JSON object a line.
const streamSignposts = bySystem({
	mac: (): Subprocess<"ignore", "pipe", "ignore"> =>
		Bun.spawn(
			[
				"/usr/bin/log",
				"stream",
				"--signpost",
				"--style",
				"ndjson",
				"--predicate",
				'subsystem == "xyz.roundedcorners.pane" AND category == "PointsOfInterest"',
			],
			{ stdout: "pipe", stderr: "ignore" },
		),
	windows: (): Subprocess<"ignore", "pipe", "ignore"> => {
		throw noWindowsSide("the app's signposts");
	},
});

// Each interval by its name, of the process `run` returns (every process's
// when it returns none).
export async function signpostFrameIntervals(
	run: () => Promise<number | undefined>,
): Promise<Record<string, FrameInterval[]>> {
	const stream = streamSignposts();

	// The stream opens before the app starts its first interval.
	await Bun.sleep(1_000);
	let pid: number | undefined;

	try {
		pid = await run();
		await Bun.sleep(1_000);
	} finally {
		stream.kill();
	}

	const begins = new Map<string, { name: string; at: number }>();
	const intervals: Record<string, FrameInterval[]> = {};

	// A field of the end's message, in milliseconds.
	const field = (message: string, name: string) => {
		const value = new RegExp(`${name}=(-?\\d+)`).exec(message)?.[1];

		return value === undefined ? undefined : Number(value) / 1000;
	};

	for (const line of (await new Response(stream.stdout).text()).split("\n")) {
		const text = line.trim().replace(/,$/, "");

		if (!text.startsWith("{")) {
			continue;
		}

		let event: Signpost;

		try {
			event = JSON.parse(text) as Signpost;
		} catch {
			continue;
		}

		if (
			event.eventType !== "signpostEvent" ||
			(pid !== undefined && event.processID !== pid) ||
			event.signpostID === undefined ||
			!event.timestamp
		) {
			continue;
		}

		const key = `${event.processID}:${event.signpostID}`;
		const at = millisecondsOf(event.timestamp);

		if (event.signpostType === "begin" && event.signpostName) {
			begins.set(key, { name: event.signpostName, at });
		} else if (event.signpostType === "end") {
			const begin = begins.get(key);

			if (begin) {
				const message = event.eventMessage ?? "";
				const heard = at - begin.at;
				const committed = field(message, "commit");
				const presented = field(message, "glass");

				begins.delete(key);
				intervals[begin.name] ??= [];

				intervals[begin.name]?.push({
					ms: heard - (field(message, "late") ?? 0),
					// A frame the GPU process didn't time says 0.
					committedMs: committed ? heard - committed : undefined,
					presentedMs: presented ? heard - presented : undefined,
				});
			}
		}
	}

	return intervals;
}

// Each interval's durations, in milliseconds, by its name, of the process
// `run` returns (every process's when it returns none): to the frame's commit
// to Core Animation where the build names it, where the Swift app's intervals
// end, the like-for-like point the shell's budgets compare
// (docs/benchmarks/views-shell.md); else to the interval's end.
export async function signpostIntervals(
	run: () => Promise<number | undefined>,
): Promise<Record<string, number[]>> {
	const intervals = await signpostFrameIntervals(run);

	return Object.fromEntries(
		Object.entries(intervals).map(([name, values]) => [
			name,
			values.map((value) => value.committedMs ?? value.ms),
		]),
	);
}
