import { stats } from "../lib/benchmark";
import { type AppState, launchPane } from "../lib/pane";
import { signpostIntervals } from "../lib/signposts";
import { expect, type Scenario } from "../run";

// H7's keys to their frames (docs/plans/views-migration.md, Phase 3), on the
// Views build, in the background. Each key is posted through AppKit's queue,
// which the menu bar takes as the user's key, and timed from the command it
// runs to the frame on screen, by the app's signposts: ⌘1 and ⌘2 between two
// tabs ("Select tab"), ⌘T ("New tab", each closed again by ⌘W), ⌘L ("Focus
// address field", from the page), Ctrl-Tab ("Open switcher", chosen by Ctrl's
// release), and ⌘Q ("Open quit dialog", Esc going back). For the Views
// build, from out/Release:
//
//   bun tests/run.ts --binary ~/chromium/src/out/Release/Pane.app/Contents/MacOS/Pane \
//     --out <dir> tests/rigs/views-key-timing.ts

const ROUNDS = 30;

const viewsKeyTiming: Scenario = async (context) => {
	const { fixtures } = context;

	const app = await launchPane(context);

	const key = (name: string, ...modifiers: string[]) =>
		app.send("key", { key: name, modifiers });

	const profile = await app.createProfile("Timing", "BLUE");
	const tabs: string[] = [];

	for (const name of ["Mail", "Docs"]) {
		const url = fixtures.url(`switcher.html?name=${name}`);
		tabs.push((await app.openPage(profile, url)).tab);
	}

	await Bun.sleep(1_000);

	const tabCount = (count: number) => (state: AppState) =>
		state.profiles.find((entry) => entry.id === profile)?.tabs.length === count;

	// Each key in a log of its own, so a tab chosen by ⌘W or the switcher's
	// choice counts as neither ⌘1's nor ⌘2's.
	const phases: [string, () => Promise<void>][] = [
		[
			"Select tab",
			async () => {
				for (let round = 0; round < ROUNDS; round++) {
					await key(round % 2 === 0 ? "1" : "2", "command");

					await app.waitFor(
						"the tab chosen",
						(state) => state.activeTabID === tabs[round % 2],
					);

					await Bun.sleep(150);
				}
			},
		],
		[
			"New tab",
			async () => {
				for (let round = 0; round < ROUNDS; round++) {
					await key("t", "command");
					await app.waitFor("the new tab", tabCount(3));
					await Bun.sleep(250);
					await key("w", "command");
					await app.waitFor("the new tab closed", tabCount(2));
					await Bun.sleep(150);
				}
			},
		],
		[
			"Focus address field",
			async () => {
				for (let round = 0; round < ROUNDS; round++) {
					await app.send("select", { tab: tabs[0] });

					await app.waitFor(
						"the page",
						(state) => state.focus === "pane.content",
					);

					await Bun.sleep(150);
					await key("l", "command");

					await app.waitFor(
						"the address",
						(state) => state.focus === "pane.toolbar.address",
					);

					await Bun.sleep(150);
				}
			},
		],
		[
			"Open switcher",
			async () => {
				for (let round = 0; round < ROUNDS; round++) {
					await key("\t", "control");
					await app.waitFor("the switcher", (state) => state.switcher.visible);
					await Bun.sleep(150);
					await app.send("dispatchKey", { key: "control", phase: "release" });
					await app.waitFor("its choice", (state) => !state.switcher.visible);
					await Bun.sleep(150);
				}
			},
		],
		[
			"Open quit dialog",
			async () => {
				for (let round = 0; round < ROUNDS; round++) {
					await key("q", "command");
					await app.waitFor("Quit Pane?", (state) => state.quitConfirmation);
					await Bun.sleep(400);
					await app.send("dispatchKey", { key: "\u001b" });
					await app.waitFor("its Cancel", (state) => !state.quitConfirmation);
					await Bun.sleep(400);
				}
			},
		],
	];

	const round = (value: number) => Math.round(value * 10) / 10;
	const timings: Record<string, unknown> = {};

	for (const [name, phase] of phases) {
		const intervals = await signpostIntervals(async () => {
			await phase();

			return app.browser.pid;
		});

		const values = [...(intervals[name] ?? [])].sort((a, b) => a - b);

		expect(values.length >= ROUNDS, `${name}: ${values.length} intervals`);

		const { median } = stats(values);

		timings[name] = {
			count: values.length,
			medianMs: round(median),
			p10Ms: round(values[Math.floor(values.length * 0.1)] ?? median),
			p90Ms: round(values[Math.floor(values.length * 0.9)] ?? median),
		};
	}

	return timings;
};

export default viewsKeyTiming;
