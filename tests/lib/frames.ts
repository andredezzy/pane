import type { Cdp } from "./cdp";

// A frame as its compositor reports it in a trace's "benchmark" category
// (cc's PipelineReporter): presented in full, in part, or dropped, and
// whether that hurt smoothness. The browser process's are the window's own
// UI; a renderer's, its page.
export type FrameReport = {
	pid: number;
	state: string;
	scroll_state: string;
	affects_smoothness: boolean;
	layer_tree_host_id: number;
	frame_source: number;
	frame_sequence: number;
};

type TraceEvent = {
	name: string;
	pid: number;
	args?: { frame_reporter?: Omit<FrameReport, "pid"> };
};

// The frames every compositor reported while `during` ran, over the
// browser's CDP connection. Only the reporter's category is traced: "cc"
// would add a hundred thousand other events a second.
export async function framesDuring(
	cdp: Cdp,
	during: () => Promise<void>,
): Promise<FrameReport[]> {
	const reports: FrameReport[] = [];

	const off = cdp.on(({ method, params, sessionId }) => {
		if (method !== "Tracing.dataCollected" || sessionId) {
			return;
		}

		for (const { name, pid, args } of params.value as TraceEvent[]) {
			if (name === "PipelineReporter" && args?.frame_reporter) {
				reports.push({ ...args.frame_reporter, pid });
			}
		}
	});

	try {
		await cdp.send("Tracing.start", {
			transferMode: "ReportEvents",
			traceConfig: { includedCategories: ["benchmark"] },
		});

		await during();

		const complete = cdp.waitFor("Tracing.tracingComplete", undefined, 60_000);
		await cdp.send("Tracing.end");
		await complete;
	} finally {
		off();
	}

	return reports;
}

// How many frames reported each state, by the process that drew them.
export function statesByProcess(
	reports: FrameReport[],
): Record<number, Record<string, number>> {
	const byProcess: Record<number, Record<string, number>> = {};

	for (const { pid, state } of reports) {
		const counts = byProcess[pid] ?? {};
		counts[state] = (counts[state] ?? 0) + 1;
		byProcess[pid] = counts;
	}

	return byProcess;
}
