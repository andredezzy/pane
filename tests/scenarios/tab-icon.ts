import { launchPane, tabOf } from "../lib/pane";
import { waitForExit } from "../lib/processes";
import { expect, type Scenario } from "../run";

// A tab's icon follows its page (the user's call of 2026-11-17, W9a and
// W9b's stale icon): a page with an icon draws it in the tab's row; a new
// document whose load ends with no icon of its own draws the globe Pane
// draws for a tab with none, as Chrome's tab shows its default icon, and
// the tab saves none, so a relaunch shows the globe too. The last page's
// icon stays only while the load runs. Before, the row kept the last page's
// icon (Google's G on every page opened from a new tab), saved with the tab.
const tabIcon: Scenario = async (context) => {
	const dataDir = context.tempDir("pane-tab-icon-");
	let app = await launchPane(context, { dataDir });
	const profile = await app.createProfile("Icons");
	const withIcon = context.fixtures.url("nav-a.html");
	const withNone = context.fixtures.url("nav-b.html");
	const { tab } = await app.openPage(profile, withIcon);

	// The tab at `url`, loaded, its saved icon and its row's both `icon`.
	const drawn = (what: string, icon: boolean, url: string) =>
		app.waitFor(
			what,
			(state) => {
				const current = tabOf(state, tab);

				return (
					current?.url === url &&
					!current.loading &&
					current.hasFavicon === icon &&
					current.rowFavicon === icon
				);
			},
			10_000,
		);

	await drawn("the page's icon in its row", true, withIcon);

	// The same tab goes to a page with no icon, its response held back: the
	// last icon while it loads, the globe once it has loaded.
	const slowNone = context.fixtures.url("nav-b.html?delay=1500");
	await app.send("navigate", { input: slowNone });

	const loading = await app.waitFor(
		"the page with none loading",
		(state) => tabOf(state, tab)?.loading === true,
		5_000,
	);

	expect(
		tabOf(loading, tab)?.rowFavicon === true,
		"the row dropped the last icon before the new page's load ended",
	);

	await drawn("the globe once the page with none loads", false, slowNone);

	// Back to the page with one: its icon again.
	await app.send("navigate", { input: withIcon });
	await drawn("the page's icon again", true, withIcon);
	await app.send("navigate", { input: withNone });
	await drawn("the globe again", false, withNone);

	// A relaunch: the tab saved no icon, so its row draws the globe, before
	// its page loads and after.
	await app.quit();
	expect(await waitForExit(app.browser.pid, 30_000), "Pane didn't end at Quit");
	app = await launchPane(context, { dataDir });

	const relaunched = await app.waitFor(
		"the profile back",
		(state) => tabOf(state, tab) !== undefined,
		30_000,
	);

	expect(
		tabOf(relaunched, tab)?.hasFavicon === false,
		"the tab saved the last page's icon through the relaunch",
	);

	await app.send("select", { tab });
	await app.page(tab, withNone);
	await drawn("the globe after the relaunch", false, withNone);
	await app.quit();
};

export default tabIcon;
