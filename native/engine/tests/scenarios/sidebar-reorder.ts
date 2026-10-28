import { type AppState, launchPane } from "../lib/pane";
import { expect, type Scenario } from "../run";

// Profiles and tabs reorder by dragging, as the Electron app's sortable lists
// did (Paper: 02.11): the drag follows the pointer up and down only and stays
// inside the list, and the order is kept on release. Each drag is paced
// through the event queue. In a window in the background SwiftUI takes the
// moves in at the release, so the checks are the orders it ends on; where the
// drop lands among rows of any height is PaneKit's ReorderTests.
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

	const initial = order(await app.state());

	expect(
		initial === "Alpha,Bravo,Charlie,Delta",
		`the profiles start as ${initial}`,
	);

	// A profile's header row: 36 points, 4 apart, the first 4 under the
	// sidebar's 39-point header.
	const rowY = (index: number) => 39 + 4 + index * 40 + 18;

	// Moves paced a frame or two apart, as a hand's; then the order the drop
	// keeps, once the copy has settled and it changed (or as it stayed).
	const drag = async (
		fromY: number,
		path: [number, number][],
		orderOf: (state: AppState) => string = order,
	) => {
		const before = orderOf(await app.state());
		await app.send("pointerWindow", { phase: "down", x: 100, y: fromY });

		for (const [x, y] of path) {
			await app.send("pointerWindow", { phase: "drag", x, y });
			await Bun.sleep(40);
		}

		const [x, y] = path.at(-1) ?? [100, fromY];
		await app.send("pointerWindow", { phase: "up", x, y });

		return app
			.waitFor("the drop", (state) => orderOf(state) !== before, 3_000)
			.catch(() => app.state());
	};

	// Alpha down past Charlie: Bravo, Charlie, Alpha, Delta.
	const steps = (fromY: number, toY: number, x = 100): [number, number][] =>
		Array.from({ length: 10 }, (_, step) => [
			x,
			fromY + ((toY - fromY) * (step + 1)) / 10,
		]);

	const down = order(await drag(rowY(0), steps(rowY(0), rowY(2) + 6)));

	expect(
		down === "Bravo,Charlie,Alpha,Delta",
		`Alpha dragged past Charlie: ${down}`,
	);

	// Delta up and far out of the sidebar, to the right and above the
	// window: the copy stays in the list, and Delta lands first.
	const out = order(
		await drag(
			rowY(3),
			steps(rowY(3), -200, 100).map(([, y], step) => [100 + step * 120, y]),
		),
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
	const bravo = ids[1] ?? "";

	const urls = ["light.html?one", "light.html?two", "light.html?three"].map(
		(each) => context.fixtures.url(each),
	);

	for (const url of urls) {
		const tab = await app.openTab(bravo, url);
		await app.page(tab, url);
	}

	const tabsOf = (state: AppState) =>
		(state.profiles.find((profile) => profile.id === bravo)?.tabs ?? [])
			.map((tab) => tab.url.split("?")[1])
			.join(",");

	const beforeTabs = tabsOf(await app.state());
	expect(beforeTabs === "one,two,three", `Bravo's tabs start as ${beforeTabs}`);

	// Bravo is second, expanded: its tabs follow its header, 32 points, 4 apart.
	const tabY = (index: number) => rowY(1) + 18 + 4 + index * 36 + 16;

	const tabs = tabsOf(await drag(tabY(0), steps(tabY(0), tabY(2) + 6), tabsOf));

	expect(tabs === "two,three,one", `the first tab dragged to the end: ${tabs}`);

	return { initial, down, out, tabs };
};

export default sidebarReorder;
