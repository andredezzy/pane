import { launchPane, tabOf, trafficLightsInPlace } from "../lib/pane";
import { expect, type Scenario } from "../run";

// The window's chrome as the Electron app lays it out: the traffic lights sit
// in the sidebar's header and the title bar spans the top 38px, over the
// toolbar. Real clicks there (posted through the window's hit testing, as the
// trackpad's) must still reach the toolbar and the header, not the title bar.
const windowChrome: Scenario = async (context) => {
	const app = await launchPane(context);

	// The traffic lights are in place from the window's first frame. AppKit
	// lays the title bar out as the engine makes the window, and they sat at
	// its own (9, 9) until the first page's title laid it out again.
	const launched = (await app.state()).window.trafficLightFrames;

	expect(
		trafficLightsInPlace(launched),
		`the traffic lights at launch: ${JSON.stringify(launched)}`,
	);

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
	// out again for it, and for the pointer over the zoom button.
	const titled = loadedB.window.trafficLightFrames;

	expect(
		trafficLightsInPlace(titled),
		`the traffic lights moved with the title: ${JSON.stringify(titled)}`,
	);

	await app.send("hoverWindow", { x: 65, y: 19 });
	await Bun.sleep(500);
	const hovered = (await app.state()).window.trafficLightFrames;
	await app.send("hoverWindow", { x: 700, y: 400 });

	expect(
		trafficLightsInPlace(hovered),
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
		lightsAtLaunch: launched,
		back: tabOf(back, tab)?.url,
		focus: focused.focus,
		expandedAfterCollapse: collapsed.profiles
			.filter((p) => p.expanded)
			.map((p) => p.name),
	};
};

export default windowChrome;
