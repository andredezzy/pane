import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { stats } from "../lib/benchmark";
import { type AppState, launchPane, type PaneApp } from "../lib/pane";
import type { Scenario } from "../run";

// From the PIN to the first page painted, and from a launch without a PIN to
// the first page painted (ADR 0113). Pane is relaunched with a launch URL on
// the data an earlier launch left, and timed on the page's own clock (its
// time origin and first contentful paint) against the runner's, on the same
// Mac. The first launch with a PIN unlocks once, as the timed ones do, so a
// one-time move of the data stays out of the timings.

const RUNS = 5;

// bcryptjs 3.0.3, hashSync("1234", 10): a PIN as the Electron app set it,
// which every Pane reads.
const PIN_1234 = {
	state: {
		pin: {
			hash: "$2b$10$6RNvGASNquZ6f5LXjFap1eMcHOZTOzRYJVRe1DrjRG2juL90W5MNO",
			length: 4,
		},
		failedAttempts: 0,
	},
	version: 0,
};

// When the page at `url` first painted, in epoch milliseconds.
async function firstPaint(app: PaneApp, url: string): Promise<number> {
	const state = await app.waitFor(
		`a tab at ${url}`,
		(state) =>
			state.profiles.some((profile) =>
				profile.tabs.some((tab) => tab.url === url),
			),
		30_000,
	);

	const tab = state.profiles
		.flatMap((profile) => profile.tabs)
		.find((tab) => tab.url === url);

	if (!tab) {
		throw new Error(`no tab at ${url}`);
	}

	const page = await app.page(tab.id, url);

	return page.evaluate<number>(`new Promise((resolve) => {
		const paint = () => performance.getEntriesByName("first-contentful-paint")[0];
		const done = () => resolve(performance.timeOrigin + paint().startTime);

		if (paint()) {
			done();
		} else {
			new PerformanceObserver(() => paint() && done()).observe({ type: "paint", buffered: true });
		}
	})`);
}

const benchmarkUnlock: Scenario = async (context) => {
	const plain = context.tempDir("pane-unlock-plain-");
	const first = await launchPane(context, { dataDir: plain });
	await first.createProfile("Work", "BLUE");
	await first.browser.close();

	// Where the time goes: the volume's attach, from the same start.
	const attach = (state: AppState, from: number) => ({
		started: (state.security.data.attachedAt?.started ?? Number.NaN) - from,
		ended: (state.security.data.attachedAt?.ended ?? Number.NaN) - from,
	});

	const withoutPin: number[] = [];
	const withoutPinAttach: { started: number; ended: number }[] = [];

	for (let run = 0; run < RUNS; run++) {
		const url = context.fixtures.url(`smoke.html?unlock=without-${run}`);
		const started = Date.now();
		const app = await launchPane(context, { dataDir: plain, args: [url] });
		withoutPin.push((await firstPaint(app, url)) - started);
		withoutPinAttach.push(attach(await app.state(), started));
		await app.browser.close();
	}

	const locked = context.tempDir("pane-unlock-pin-");
	mkdirSync(locked, { recursive: true });
	writeFileSync(join(locked, "security.json"), JSON.stringify(PIN_1234));

	const setup = await launchPane(context, { dataDir: locked });
	await setup.send("pinKeys", { keys: "1234" });

	await setup.waitFor(
		"unlocked",
		(state) => !state.security.locked && state.security.data.phase === "open",
		60_000,
	);

	await setup.createProfile("Work", "BLUE");
	await setup.browser.close();

	const fromPin: number[] = [];
	const fromPinAttach: { started: number; ended: number }[] = [];

	for (let run = 0; run < RUNS; run++) {
		const url = context.fixtures.url(`smoke.html?unlock=pin-${run}`);
		const app = await launchPane(context, { dataDir: locked, args: [url] });

		await app.waitFor(
			"the PIN screen",
			(state) => state.security.screen?.mode === "unlock",
		);

		const sent = Date.now();
		await app.send("pinKeys", { keys: "1234" });
		fromPin.push((await firstPaint(app, url)) - sent);
		fromPinAttach.push(attach(await app.state(), sent));
		await app.browser.close();
	}

	const of = (runs: { started: number; ended: number }[]) => ({
		started: stats(runs.map((run) => run.started)),
		ended: stats(runs.map((run) => run.ended)),
	});

	return {
		withoutPin: stats(withoutPin),
		fromPin: stats(fromPin),
		attach: { withoutPin: of(withoutPinAttach), fromPin: of(fromPinAttach) },
	};
};

export default benchmarkUnlock;
