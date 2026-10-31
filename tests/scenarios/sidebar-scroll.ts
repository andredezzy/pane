import { isScreenLocked } from "../lib/cua";
import { launchPane } from "../lib/pane";
import { poll } from "../lib/poll";
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

	const select = async (tabID: string) => {
		await app.send("select", { tab: tabID });

		await app.waitFor(
			"the tab selected",
			(state) => state.activeTabID === tabID,
		);
	};

	// The frames once the list shows the tab's row whole and holds still, two
	// reads alike: it scrolls with an animation.
	const inView = (tabID: string, description: string) => {
		let previous = "";

		return poll(description, async () => {
			const seen = await frames();
			const read = JSON.stringify(seen);
			const settled = read === previous && shows(seen, tabID);
			previous = read;

			return settled && seen;
		});
	};

	// The last tab, opened last, is selected at the list's end.
	const opened = await inView(last, "the last tab opened, in view");
	expect(!shows(opened, first), "the list is short enough to show every tab");

	// The first tab, chosen away from the sidebar: the list scrolls up to it.
	await select(first);
	const up = await inView(first, "the first tab, selected, in view");

	// A tab in view: the list holds still.
	await select(second);
	await Bun.sleep(700);
	const still = await frames();

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

	await inView(last, "the switcher's tab, in view");
	const chosen = last;

	return {
		lastOpened: opened[`pane.tab.${last}`],
		firstSelected: up[`pane.tab.${first}`],
		list: up["pane.sidebar.profiles"],
		switcherChose: tabs.indexOf(chosen),
	};
};

export default sidebarScroll;
