import { isScreenLocked } from "../lib/cua";
import { type AppState, launchPane } from "../lib/pane";
import { swiftTool } from "../lib/tools";
import { expect, type Scenario } from "../run";

// Profiles and tabs reorder by dragging, as the Electron app's sortable lists
// did (Paper: 03.11): the drag follows the pointer up and down only and stays
// inside the list, and the order is kept on release. Each drag is paced
// through the event queue. Before the release the rows already stand where
// the drop would put them (the user's report: nothing moved until the
// release), and a release on the dragged row is no click. Where the drop
// lands among rows of any height is PaneKit's ReorderTests.
const sidebarReorder: Scenario = async (context) => {
	const app = await launchPane(context);
	const names = ["Alpha", "Bravo", "Charlie", "Delta"];
	const ids: string[] = [];

	for (const name of names) {
		ids.push(await app.createProfile(name));
	}

	// Collapsed, each profile is its header alone.
	for (const id of ids) {
		await app.send("unloadProfile", { profile: id });
	}

	await app.waitFor("the profiles collapsed", (state) =>
		state.profiles.every((profile) => !profile.expanded),
	);

	const order = (state: AppState) =>
		state.profiles.map((profile) => profile.name).join(",");

	const expanded = (state: AppState) =>
		state.profiles.filter((profile) => profile.expanded).map((p) => p.name);

	const initial = order(await app.state());

	expect(
		initial === "Alpha,Bravo,Charlie,Delta",
		`the profiles start as ${initial}`,
	);

	// A profile's header row: 36 points, 4 apart, the first 4 under the
	// sidebar's 39-point header.
	const rowY = (index: number) => 39 + 4 + index * 40 + 18;

	// The rows' tops as the window lays them out, read through accessibility
	// (hidden from it while the screen is locked), by the ids after `prefix`.
	const locked = await isScreenLocked();
	const axFrames = await swiftTool("ax-frames");

	const tops = async (prefix: string) => {
		const frames = JSON.parse(
			await Bun.$`${axFrames} ${app.browser.pid} ${prefix}`.text(),
		) as Record<string, number[]>;

		return Object.fromEntries(
			Object.entries(frames).map(([id, frame]) => [
				id.slice(prefix.length),
				frame[1] ?? Number.NaN,
			]),
		);
	};

	const midways: Record<string, Record<string, number>> = {};

	// Before the release, each row's top is where the drop would put it:
	// `rows` lists the ids in that order, from the first row's top, `pitch`
	// apart, and the copy rides the pointer over them.
	const standsAt =
		(
			name: string,
			prefix: string,
			rows: string[],
			firstTop: number,
			pitch: number,
		) =>
		async () => {
			if (locked) {
				return;
			}

			// The rows slide aside on a 0.28-second spring.
			await Bun.sleep(400);
			const seen = await tops(prefix);
			midways[name] = seen;

			rows.forEach((id, index) => {
				const top = seen[id] ?? Number.NaN;

				expect(
					Math.abs(top - (firstTop + index * pitch)) <= 1,
					`${name}: before the release, row ${index} is not ${id}: ${JSON.stringify(seen)}`,
				);
			});
		};

	// Moves paced a frame or two apart, as a hand's; then the order the drop
	// keeps, once the copy has settled and it changed (or as it stayed).
	const drag = async (
		fromY: number,
		path: [number, number][],
		orderOf: (state: AppState) => string = order,
		midway?: () => Promise<void>,
	) => {
		const before = orderOf(await app.state());
		await app.send("pointerWindow", { phase: "down", x: 100, y: fromY });

		for (const [x, y] of path) {
			await app.send("pointerWindow", { phase: "drag", x, y });
			await Bun.sleep(40);
		}

		await midway?.();

		const [x, y] = path.at(-1) ?? [100, fromY];
		await app.send("pointerWindow", { phase: "up", x, y });

		const after = await app
			.waitFor("the drop", (state) => orderOf(state) !== before, 3_000)
			.catch(() => app.state());

		// A click would land a moment after the drop.
		await Bun.sleep(300);

		return { after, settled: await app.state() };
	};

	const [alpha = "", bravo = "", charlie = "", delta = ""] = ids;

	// Alpha down past Charlie: Bravo, Charlie, Alpha, Delta. The release is
	// on Alpha's own row, which stands there by then.
	const steps = (fromY: number, toY: number, x = 100): [number, number][] =>
		Array.from({ length: 10 }, (_, step) => [
			x,
			fromY + ((toY - fromY) * (step + 1)) / 10,
		]);

	const first = await drag(
		rowY(0),
		steps(rowY(0), rowY(2) + 6),
		order,
		standsAt(
			"Alpha past Charlie",
			"pane.profile.",
			[bravo, charlie, alpha, delta],
			rowY(0) - 18,
			40,
		),
	);

	const down = order(first.after);

	expect(
		down === "Bravo,Charlie,Alpha,Delta",
		`Alpha dragged past Charlie: ${down}`,
	);

	expect(
		expanded(first.settled).length === 0,
		`the release on Alpha's row clicked it: ${expanded(first.settled)}`,
	);

	// Delta up and far out of the sidebar, to the right and above the
	// window: the copy stays in the list, and Delta lands first.
	const out = order(
		(
			await drag(
				rowY(3),
				steps(rowY(3), -200, 100).map(([, y], step) => [100 + step * 120, y]),
			)
		).after,
	);

	expect(
		out === "Delta,Bravo,Charlie,Alpha",
		`Delta dragged up and out: ${out}`,
	);

	// A click with no move still toggles the profile: no drag below 5 points.
	await app.send("pointerWindow", { phase: "down", x: 100, y: rowY(1) });
	await app.send("pointerWindow", { phase: "drag", x: 102, y: rowY(1) + 2 });
	await app.send("pointerWindow", { phase: "up", x: 102, y: rowY(1) + 2 });

	const clicked = await app.waitFor(
		"Bravo expanded",
		(state) =>
			state.profiles.find((profile) => profile.name === "Bravo")?.expanded ===
			true,
	);

	expect(
		order(clicked) === out,
		`a click moved the profiles: ${order(clicked)}`,
	);

	// Tabs in a profile: the first one dragged to the end.
	const urls = ["light.html?one", "light.html?two", "light.html?three"].map(
		(each) => context.fixtures.url(each),
	);

	const tabIDs: string[] = [];

	for (const url of urls) {
		const tab = await app.openTab(bravo, url);
		await app.page(tab, url);
		tabIDs.push(tab);
	}

	const tabsOf = (state: AppState) =>
		(state.profiles.find((profile) => profile.id === bravo)?.tabs ?? [])
			.map((tab) => tab.url.split("?")[1])
			.join(",");

	const beforeTabs = await app.state();

	expect(
		tabsOf(beforeTabs) === "one,two,three",
		`Bravo's tabs start as ${tabsOf(beforeTabs)}`,
	);

	// Bravo is second, expanded: its tabs follow its header, 32 points, 4 apart.
	const tabY = (index: number) => rowY(1) + 18 + 4 + index * 36 + 16;
	const [one = "", two = "", three = ""] = tabIDs;

	const tabDrag = await drag(
		tabY(0),
		steps(tabY(0), tabY(2) + 6),
		tabsOf,
		standsAt(
			"the first tab to the end",
			"pane.tab.",
			[two, three, one],
			tabY(0) - 16,
			36,
		),
	);

	const tabs = tabsOf(tabDrag.after);

	expect(tabs === "two,three,one", `the first tab dragged to the end: ${tabs}`);

	// The release lands on the first tab's row, at the end by then: the
	// selected tab stays the last one opened.
	expect(
		tabDrag.settled.activeTabID === beforeTabs.activeTabID,
		`the release on the dragged tab selected it: ${tabDrag.settled.activeTabID}`,
	);

	if (locked) {
		context.defer("the rows mid-drag: accessibility is off on a locked screen");
	}

	return { initial, down, out, tabs, midways };
};

export default sidebarReorder;
