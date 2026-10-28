// Launches the Pane app with an engine trace, for scenarios that check the
// app↔engine traffic.
import { existsSync, readFileSync, rmSync } from "node:fs";
import { join } from "node:path";
import type { ScenarioContext } from "../run";
import { launchPane, type PaneApp, type PaneLaunch } from "./pane";

export type TraceEntry = {
	kind: string;
	name: string;
	args: Record<string, unknown>;
};

export type Traced = {
	app: PaneApp;
	trace: () => TraceEntry[];
	has: (
		name: string,
		matches?: (args: Record<string, unknown>) => boolean,
	) => boolean;
	// The first entry named `name` that `matches`, once the trace has one.
	waitForEntry: (
		name: string,
		matches?: (args: Record<string, unknown>) => boolean,
		timeoutMs?: number,
	) => Promise<TraceEntry>;
};

export async function launchTraced(
	context: ScenarioContext,
	name: string,
	options: PaneLaunch = {},
): Promise<Traced> {
	const path = join(context.out, `${name}-trace.jsonl`);
	rmSync(path, { force: true });

	const app = await launchPane(context, {
		...options,
		env: { ...options.env, PANE_ENGINE_TRACE: path },
	});

	const trace = () =>
		existsSync(path)
			? readFileSync(path, "utf8")
					.split("\n")
					.filter(Boolean)
					.map((line) => JSON.parse(line) as TraceEntry)
			: [];

	const find = (
		entry: string,
		matches: (args: Record<string, unknown>) => boolean,
	) => trace().find((line) => line.name === entry && matches(line.args));

	return {
		app,
		trace,
		has: (entry, matches = () => true) => find(entry, matches) !== undefined,
		waitForEntry: async (entry, matches = () => true, timeoutMs = 20_000) => {
			const deadline = Date.now() + timeoutMs;

			for (;;) {
				const found = find(entry, matches);

				if (found) {
					return found;
				}

				if (Date.now() > deadline) {
					throw new Error(`the trace has no ${entry}`);
				}

				await Bun.sleep(100);
			}
		},
	};
}

// Presses the left (or right) mouse button over a point of the page, as a
// user would: input that counts as a user gesture.
export async function clickAt(
	send: (method: string, params?: Record<string, unknown>) => Promise<unknown>,
	x: number,
	y: number,
	button: "left" | "right" = "left",
): Promise<void> {
	for (const type of ["mousePressed", "mouseReleased"]) {
		await send("Input.dispatchMouseEvent", {
			type,
			x,
			y,
			button,
			buttons: button === "left" ? 1 : 2,
			clickCount: 1,
		});
	}
}
