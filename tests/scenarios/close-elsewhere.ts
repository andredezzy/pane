import { launchPane, tabOf } from "../lib/pane";
import { expect, type Scenario } from "../run";

// A tab closed in a profile that isn't the one showing leaves the window
// where it is (W9a, found by hand in the test VM): closing the tab a hidden
// profile last showed has Chrome's tab strip of that profile's Browser make
// its next page active, which Pane took for an extension's request to show
// it, and the window moved to that profile. The tab's X and its menu's Close
// tab both close through the channel's closeTab, as here.
const closeElsewhere: Scenario = async (context) => {
	const app = await launchPane(context);
	const work = await app.createProfile("Work");
	const first = await app.openPage(work, context.fixtures.url("nav-a.html"));
	// Work's last shown tab, its strip's active one.
	const last = await app.openPage(work, context.fixtures.url("nav-b.html"));
	const other = await app.createProfile("Other");
	const shown = await app.openPage(other, context.fixtures.url("smoke.html"));

	await app.waitFor(
		"Other's tab showing",
		(state) =>
			state.activeTabID === shown.tab && state.activeProfileID === other,
	);

	// Work's tab its strip has active, closed while Other shows.
	await app.send("closeTab", { tab: last.tab });

	await app.waitFor(
		"Work's tab closed",
		(state) => tabOf(state, last.tab) === undefined,
	);

	// The selection would have moved on the turn after the close.
	await Bun.sleep(1_000);
	const after = await app.state();

	expect(
		after.activeTabID === shown.tab && after.activeProfileID === other,
		`closing Work's tab moved the window to ${after.activeProfileID}'s ${after.activeTabID}`,
	);

	expect(
		tabOf(after, first.tab) !== undefined,
		"Work's other tab went with the closed one",
	);

	// The same for a tab Work's strip doesn't have active: nothing moves.
	await app.send("closeTab", { tab: first.tab });

	await app.waitFor(
		"Work's other tab closed",
		(state) => tabOf(state, first.tab) === undefined,
	);

	await Bun.sleep(1_000);
	const end = await app.state();

	expect(
		end.activeTabID === shown.tab && end.activeProfileID === other,
		`the second close moved the window to ${end.activeProfileID}'s ${end.activeTabID}`,
	);

	return { stayed: shown.tab };
};

export default closeElsewhere;
