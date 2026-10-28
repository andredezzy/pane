import { launchPane, tabOf } from "../lib/pane";
import { expect, type Scenario } from "../run";

// Selecting a tab gives its page the focus, as Chrome's tab switch does, and
// never the address field (the user's report: entering a tab selected the
// address and focused it), and so does coming back from Settings. A new tab
// with no page focuses the address field, as before.
const tabFocus: Scenario = async (context) => {
	const app = await launchPane(context);
	const profile = await app.createProfile("Focus");
	const a = context.fixtures.url("nav-a.html");
	const b = context.fixtures.url("nav-b.html");
	const first = await app.openTab(profile, a);
	await app.page(first, a);
	const second = await app.openTab(profile, b);
	await app.page(second, b);

	await app.waitFor(
		"both loaded",
		(state) =>
			tabOf(state, first)?.loading === false &&
			tabOf(state, second)?.loading === false,
	);

	// The page has the focus, as after a click in it.
	await app.send("clickPage", { x: 20, y: 20 });

	await app.waitFor(
		"the page focused",
		(state) => state.focus === "RenderWidgetHostViewCocoa",
	);

	// The first tab's row in the sidebar: under the profile's header, 32
	// points, 4 apart.
	const rowY = 39 + 4 + 36 + 4 + 16;
	await app.send("clickWindow", { x: 100, y: rowY });

	await app.waitFor(
		"the first tab selected",
		(state) => state.activeTabID === first,
	);

	// Focus moves on the turn after the click; then it holds.
	await Bun.sleep(400);
	const selected = await app.state();

	expect(
		selected.focus === "RenderWidgetHostViewCocoa",
		`selecting a tab focused ${selected.focus}`,
	);

	// Back by the switcher's keys, as Ctrl-Tab does.
	await app.send("switcher", { action: "next" });
	await app.send("switcher", { action: "commit" });

	await app.waitFor(
		"the second tab selected",
		(state) => state.activeTabID === second,
	);

	await Bun.sleep(400);
	const switched = await app.state();

	expect(
		switched.focus === "RenderWidgetHostViewCocoa",
		`the switcher focused ${switched.focus}`,
	);

	// Settings, then back to the browser: the page again.
	await app.send("showPage", { page: "settings" });
	await app.send("showPage", { page: "browser" });
	await Bun.sleep(400);
	const back = await app.state();

	expect(
		back.focus === "RenderWidgetHostViewCocoa",
		`back from Settings, ${back.focus} had the focus`,
	);

	// A new tab with no page: the address field, to type in.
	await app.send("menu", { path: ["File", "New Tab"] });
	await Bun.sleep(400);
	const fresh = await app.state();

	expect(
		fresh.focus === "field: Search or enter URL",
		`a new tab focused ${fresh.focus}`,
	);

	return {
		selected: selected.focus,
		switched: switched.focus,
		backFromSettings: back.focus,
		newTab: fresh.focus,
	};
};

export default tabFocus;
