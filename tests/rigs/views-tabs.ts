import { tabOf } from "../lib/pane";
import { poll } from "../lib/poll";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// H5, the tabs (docs/plans/views-migration.md, Phase 3), on the Views build,
// in the background. Its renders, for the Swift's: the tab switcher with two
// groups (surfaces' switcher-open), one group (shortcuts' switcher), one tab
// alone, and its selection moved on; and the sad tab (surfaces'
// sad-tab-crashed). For the Views build, from out/Release:
//
//   bun tests/run.ts --binary ~/chromium/src/out/Release/Pane.app/Contents/MacOS/Pane \
//     --out <dir> [--appearance light|dark] tests/rigs/views-tabs.ts

const viewsTabs: Scenario = async (context) => {
	const { fixtures } = context;
	const { app, trace } = await launchTraced(context, "views-tabs");

	const page = (name: string) =>
		fixtures.url(`switcher.html?name=${encodeURIComponent(name)}`);

	// One tab alone: selected, as there is no other.
	const alone = await app.createProfile("Alone", "VIOLET");
	await app.loadTab(alone, page("Only tab"));
	await app.send("switcher", { action: "next" });

	const one = await app.waitFor(
		"the switcher on one tab",
		(state) => state.switcher.visible,
	);

	expect(
		one.switcher.selectedIndex === 0 &&
			one.switcher.groups.flatMap((group) => group.tabs).length === 1,
		`one tab's switcher: ${JSON.stringify(one.switcher)}`,
	);

	await app.render("switcher-one-tab.png");
	await app.send("switcher", { action: "cancel" });
	await app.send("unloadProfile", { profile: alone });

	// One group of two, the other selected, as shortcuts' render.
	const keys = await app.createProfile("Keys", "BLUE");
	await app.loadTab(keys, fixtures.url("nav-b.html"));
	await app.loadTab(keys, fixtures.url("nav-a.html?keys"));
	await app.send("switcher", { action: "next" });
	await app.waitFor("the switcher on Keys", (state) => state.switcher.visible);
	await app.render("switcher-one-group.png");
	await app.send("switcher", { action: "cancel" });
	await app.send("unloadProfile", { profile: keys });

	// Two groups, as surfaces' switcher-open: Work's Navigation A, then Docs,
	// then Calendar; Proxied's one tab. It opens on Docs, the tab used before.
	const work = await app.createProfile("Work", "BLUE");
	const calendar = await app.loadTab(work, page("Calendar"));
	const docs = await app.loadTab(work, page("Docs"));
	const proxied = await app.createProfile("Proxied", "EMERALD");
	const through = await app.loadTab(proxied, page("Through the proxy"));
	const home = await app.loadTab(work, fixtures.url("nav-a.html?work"));
	await app.send("select", { tab: docs });
	await app.send("select", { tab: home });

	await app.waitFor("Navigation A's favicon", (state) => {
		const tab = tabOf(state, home);

		return tab?.hasFavicon === true && state.activeTabID === home;
	});

	await app.send("switcher", { action: "next" });

	const open = await app.waitFor(
		"the switcher on two groups",
		(state) => state.switcher.visible,
	);

	expect(
		JSON.stringify(open.switcher.groups.map((group) => group.tabs)) ===
			JSON.stringify([[home, docs, calendar], [through]]) &&
			open.switcher.selectedIndex === 1,
		`the switcher's groups: ${JSON.stringify(open.switcher)}`,
	);

	await app.render("switcher-open.png");

	// Its selection moved on, at once.
	await app.send("switcher", { action: "next" });

	await app.waitFor(
		"the selection on Calendar",
		(state) => state.switcher.selectedIndex === 2,
	);

	await app.render("switcher-moved.png");

	const frames = await app.frames("pane.switcher");

	expect(
		(frames["pane.switcher"]?.[2] ?? 0) === 320,
		`the switcher's frames: ${JSON.stringify(frames)}`,
	);

	await app.send("switcher", { action: "commit" });

	await app.waitFor(
		"Calendar chosen",
		(state) => !state.switcher.visible && state.activeTabID === calendar,
	);

	// The sad tab: a page that crashes twice in a minute.
	const crashes = () =>
		trace().filter(
			(entry) => entry.name === "PaneWebContentsDelegate.rendererDidTerminate",
		).length;

	const url = fixtures.url("crash.html?sad");
	const crashing = await app.openTab(work, url);
	const before = crashes();

	for (let crash = before + 1; crash <= before + 2; crash++) {
		const crashed = await app.page(crashing, url);
		crashed.send("Page.crash").catch(() => {});
		await poll(`crash ${crash}`, () => crashes() >= crash, 15_000, 150);
	}

	await app.waitFor("the sad tab", (state) =>
		state.crashedTabs.includes(crashing),
	);

	await app.render("sad-tab-crashed.png");
};

export default viewsTabs;
