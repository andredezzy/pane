import { stats } from "../lib/benchmark";
import { launchPane } from "../lib/pane";
import { expect, type Scenario } from "../run";

// How long each signposted interaction (Log.interaction) takes, from the
// input to the frame Core Animation commits for it, read from the unified
// log as the app runs: selecting a tab among five loaded ones, a new tab
// from the File menu, and focusing the address field. It times; it does
// not judge, so a change is measured before and after.
// /usr/bin/log, by its path: zsh has a builtin of the same name.

type Signpost = {
	eventType?: string;
	signpostType?: "begin" | "end" | "event";
	signpostName?: string;
	signpostID?: number;
	processID?: number;
	timestamp?: string;
};

const ROUNDS = 8;

// The log's "2026-10-30 15:41:02.123456-0300", in milliseconds.
function millisecondsOf(timestamp: string): number {
	const [date, time] = timestamp.split(" ");
	const [clock = "", fraction = "0"] = (time ?? "").slice(0, 15).split(".");

	return Date.parse(`${date}T${clock}Z`) + Number(`0.${fraction}`) * 1000;
}

const interactionTiming: Scenario = async (context) => {
	const stream = Bun.spawn(
		[
			"/usr/bin/log",
			"stream",
			"--signpost",
			"--style",
			"ndjson",
			"--predicate",
			'subsystem == "com.pane.desktop" AND category == "PointsOfInterest"',
		],
		{ stdout: "pipe", stderr: "ignore" },
	);

	// The stream opens before the app starts its first interval.
	await Bun.sleep(1_000);

	const app = await launchPane(context);
	const profile = await app.createProfile("Timing");
	const tabs: string[] = [];

	for (const name of ["Mail", "Docs", "Calendar", "News", "Music"]) {
		const url = context.fixtures.url(`switcher.html?name=${name}`);
		const tab = await app.openTab(profile, url);
		await app.page(tab, url);
		tabs.push(tab);
	}

	await Bun.sleep(1_000);

	for (let round = 0; round < ROUNDS; round++) {
		for (const tab of tabs) {
			await app.send("select", { tab });
			await Bun.sleep(150);
		}
	}

	for (let i = 0; i < ROUNDS * 2; i++) {
		await app.send("menu", { path: ["File", "New Tab"] });
		await Bun.sleep(250);
		await app.send("select", { tab: tabs[i % tabs.length] });
		await Bun.sleep(150);
		await app.send("menu", { path: ["Navigate", "Focus Address Bar"] });
		await Bun.sleep(150);
	}

	await Bun.sleep(1_000);
	stream.kill();

	const pid = app.browser.pid;
	const begins = new Map<number, { name: string; at: number }>();
	const durations: Record<string, number[]> = {};

	for (const line of (await new Response(stream.stdout).text()).split("\n")) {
		const text = line.trim().replace(/,$/, "");

		if (!text.startsWith("{")) {
			continue;
		}

		const event = JSON.parse(text) as Signpost;

		if (
			event.eventType !== "signpostEvent" ||
			event.processID !== pid ||
			event.signpostID === undefined ||
			!event.timestamp
		) {
			continue;
		}

		const at = millisecondsOf(event.timestamp);

		if (event.signpostType === "begin" && event.signpostName) {
			begins.set(event.signpostID, { name: event.signpostName, at });
		} else if (event.signpostType === "end") {
			const begin = begins.get(event.signpostID);

			if (begin) {
				begins.delete(event.signpostID);

				durations[begin.name] = [
					...(durations[begin.name] ?? []),
					at - begin.at,
				];
			}
		}
	}

	expect(
		["Select tab", "New tab", "Focus address field"].every(
			(name) => (durations[name]?.length ?? 0) > 0,
		),
		`the log held no intervals for ${JSON.stringify(Object.keys(durations))}`,
	);

	return Object.fromEntries(
		Object.entries(durations).map(([name, values]) => {
			const { median, min, max } = stats(values);
			const sorted = [...values].sort((a, b) => a - b);
			const p90 = sorted[Math.floor(sorted.length * 0.9)] ?? max;
			const round = (value: number) => Math.round(value * 10) / 10;

			return [
				name,
				{
					count: values.length,
					medianMs: round(median),
					p90Ms: round(p90),
					minMs: round(min),
					maxMs: round(max),
				},
			];
		}),
	);
};

export default interactionTiming;
