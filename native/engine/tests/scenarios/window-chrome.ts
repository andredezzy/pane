import { launchPane, tabOf } from "../lib/pane";
import { expect, type Scenario } from "../run";

// The window's chrome as the Electron app lays it out: the traffic lights sit
// in the sidebar's header and the title bar spans the top 40px, over the
// toolbar. Real clicks there (posted through the window's hit testing, as the
// trackpad's) must still reach the toolbar and the header, not the title bar.
const windowChrome: Scenario = async (context) => {
	const app = await launchPane(context);
	const profile = await app.createProfile("Chrome");
	const a = context.fixtures.url("nav-a.html");
	const b = context.fixtures.url("nav-b.html");
	const tab = await app.openTab(profile, a);
	await app.page(tab, a);

	await app.send("navigate", { input: b });

	const loadedB = await app.waitFor(
		"B loaded",
		(state) => tabOf(state, tab)?.url === b && !tabOf(state, tab)?.loading,
	);

	// The window's title follows the page's, and AppKit lays the title bar
	// out again for it, and for the pointer over the zoom button: the
	// traffic lights stay in place, all three on one line from (12, 12),
	// evenly apart.
	const inPlace = (lights: number[][]) => {
		const [close, miniaturize, zoom] = lights;
		const spacing = (miniaturize?.[0] ?? 0) - (close?.[0] ?? 0);

		return (
			close?.[0] === 12 &&
			lights.every((frame) => frame[1] === 12) &&
			spacing > 0 &&
			(zoom?.[0] ?? 0) - (miniaturize?.[0] ?? 0) === spacing
		);
	};

	const titled = loadedB.window.trafficLightFrames;

	expect(
		inPlace(titled),
		`the traffic lights moved with the title: ${JSON.stringify(titled)}`,
	);

	await app.send("hoverWindow", { x: 65, y: 19 });
	await Bun.sleep(500);
	const hovered = (await app.state()).window.trafficLightFrames;
	await app.send("hoverWindow", { x: 700, y: 400 });

	expect(
		inPlace(hovered),
		`the traffic lights moved under the pointer: ${JSON.stringify(hovered)}`,
	);

	// Back: a 32-point circle at (265, 13) in the window, past the 260-point
	// sidebar.
	await app.send("clickWindow", { x: 281, y: 29 });

	const back = await app.waitFor(
		"back to A",
		(state) => tabOf(state, tab)?.url === a,
	);

	// The address field, under the title bar.
	await app.send("clickWindow", { x: 700, y: 29 });

	const focused = await app.waitFor(
		"the address field focused",
		(state) => state.focus === "field: Search or enter URL",
	);

	// Collapse all, a 28-point circle at the end of the sidebar's header.
	await app.send("clickWindow", { x: 236, y: 19.5 });

	const collapsed = await app.waitFor(
		"profiles collapsed",
		(state) =>
			state.activeTabID === null || !state.profiles.some((p) => p.expanded),
	);

	return {
		back: tabOf(back, tab)?.url,
		focus: focused.focus,
		expandedAfterCollapse: collapsed.profiles
			.filter((p) => p.expanded)
			.map((p) => p.name),
	};
};

export default windowChrome;
