import { launchPane, tabOf } from "../lib/pane";
import { type Scenario } from "../run";

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

	await app.waitFor(
		"B loaded",
		(state) => tabOf(state, tab)?.url === b && !tabOf(state, tab)?.loading,
	);

	// Back: 26px at (226, 16) in the window.
	await app.send("clickWindow", { x: 239, y: 29 });

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

	// Collapse all, in the sidebar's header beside the traffic lights.
	await app.send("clickWindow", { x: 198, y: 19.5 });

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
