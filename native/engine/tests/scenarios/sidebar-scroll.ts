import { isScreenLocked } from "../lib/cua";
import { launchPane } from "../lib/pane";
import { swiftTool } from "../lib/tools";
import { expect, type Scenario } from "../run";

// The selected tab's row comes into the sidebar's view, wherever the tab was
// chosen (the user's request): the switcher, a shortcut, a new tab. The list
// scrolls the least that shows the row whole, and not at all when it shows.
const sidebarScroll: Scenario = async (context) => {
	const app = await launchPane(context);

	// Accessibility, which reads the rows' frames, is off on a locked screen.
	if (await isScreenLocked()) {
		context.defer(
			"the sidebar's scroll: accessibility is off on a locked screen",
		);

		return {};
	}

	const profile = await app.createProfile("Scroll");
	const tabs: string[] = [];

	// More rows than the list shows: 24 tabs of 36 points.
	for (let index = 0; index < 24; index++) {
		tabs.push(
			await app.openTab(profile, context.fixtures.url(`light.html?${index}`)),
		);
	}

	const [first = "", second = "", last = ""] = [tabs[0], tabs[1], tabs.at(-1)];
	const axFrames = await swiftTool("ax-frames");

	// Each row's frame and the list's, from the window's top left.
	const frames = async () =>
		JSON.parse(
			await Bun.$`${axFrames} ${app.browser.pid} pane.`.text(),
		) as Record<string, number[]>;

	// Whole inside the list's visible frame.
	const shows = (seen: Record<string, number[]>, tabID: string) => {
		const [, listTop = 0, , listHeight = 0] =
			seen["pane.sidebar.profiles"] ?? [];

		const [, top = Number.NaN, , height = 0] = seen[`pane.tab.${tabID}`] ?? [];

		return top >= listTop && top + height <= listTop + listHeight;
	};

	// Once the selection has moved and the list has scrolled to it.
	const select = async (tabID: string) => {
		await app.send("select", { tab: tabID });

		await app.waitFor(
			"the tab selected",
			(state) => state.activeTabID === tabID,
		);

		await Bun.sleep(700);

		return frames();
	};

	// The last tab, opened last, is selected at the list's end.
	await Bun.sleep(700);
	const opened = await frames();

	expect(shows(opened, last), "the last tab opened is out of view");
	expect(!shows(opened, first), "the list is short enough to show every tab");

	// The first tab, chosen away from the sidebar: the list scrolls up to it.
	const up = await select(first);
	expect(shows(up, first), "the first tab, selected, stayed out of view");

	// A tab in view: the list holds still.
	const still = await select(second);

	expect(
		still[`pane.tab.${first}`]?.[1] === up[`pane.tab.${first}`]?.[1],
		`selecting a tab in view scrolled the list: ${up[`pane.tab.${first}`]?.[1]} to ${still[`pane.tab.${first}`]?.[1]}`,
	);

	// By the switcher's keys, twice Ctrl-Tab: the third most recent tab, the
	// last, down at the list's end.
	await app.send("switcher", { action: "next" });
	await app.send("switcher", { action: "next" });
	await app.send("switcher", { action: "commit" });

	await app.waitFor(
		"the switcher's tab",
		(state) => state.activeTabID === last,
	);

	await Bun.sleep(700);
	const switched = await frames();
	const chosen = last;

	expect(shows(switched, last), "the switcher's tab stayed out of view");

	return {
		lastOpened: opened[`pane.tab.${last}`],
		firstSelected: up[`pane.tab.${first}`],
		list: up["pane.sidebar.profiles"],
		switcherChose: tabs.indexOf(chosen),
	};
};

export default sidebarScroll;
