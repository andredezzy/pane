// The app's signposts, Instruments' Points of Interest (Log.interaction, and
// the Views build's pane/ui/interaction_log.h): each interval from its input
// to the frame it answers with, read from the unified log while `run` drives
// the app. /usr/bin/log by its path: zsh has a builtin of the same name.

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

// Each interval's durations, in milliseconds, by its name, of the process
// `run` returns (every process's when it returns none).
export async function signpostIntervals(
	run: () => Promise<number | undefined>,
): Promise<Record<string, number[]>> {
	const stream = Bun.spawn(
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
	);

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
	const intervals: Record<string, number[]> = {};

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
				// The Views build ends an interval when the UI thread hears its
				// frame reached the screen, and says how long before that was
				// (late=, in microseconds): the interval ends at the frame.
				const late =
					Number(/late=(\d+)/.exec(event.eventMessage ?? "")?.[1] ?? 0) / 1000;

				begins.delete(key);
				intervals[begin.name] ??= [];
				intervals[begin.name]?.push(at - late - begin.at);
			}
		}
	}

	return intervals;
}
