import { appOf } from "../lib/browser";
import {
	launchPane,
	type PaneApp,
	tabOf,
	trafficLightsInPlace,
} from "../lib/pane";
import { swiftTool } from "../lib/tools";
import { expect, type Scenario, type ScenarioContext } from "../run";

// H1, the window (docs/plans/views-migration.md, Phase 3), on the Views build,
// in the background. Its renders, empty and with a page, for the Swift's
// (profile-lifecycle's empty-state.png, surfaces' window-two-profiles.png);
// its title following the shown page's, Pane without one; and its close
// sequence and new windows: the close button, a real click, closes it, every
// page with it and every tab kept (ADR 0016), and the app opened again (a
// reopen, as a Dock click sends one, through Launch Services) or handed a
// URL (ADR 0109) shows a new window, its selected tab's page made again,
// its traffic lights in place. For the Views build, from out/Release:
//
//   bun tests/run.ts --binary ~/chromium/src/out/Release/Pane.app/Contents/MacOS/Pane \
//     --out <dir> [--appearance light|dark] tests/rigs/views-window.ts

// What another app hands Pane, through Launch Services (tools/open-urls.swift):
// with no URL, the app opened as its Dock icon opens it.
async function handOver(
	context: ScenarioContext,
	app: PaneApp,
	urls: string[],
): Promise<void> {
	const handed =
		await Bun.$`${await swiftTool("open-urls")} ${appOf(context.binary)} ${app.browser.pid} ${context.tempDir("pane-window-home-")} ${urls}`
			.quiet()
			.nothrow();

	expect(
		handed.exitCode === 0,
		`open-urls failed: ${handed.stderr.toString().trim()}`,
	);
}

async function closeWindow(app: PaneApp) {
	// The close button, at the traffic lights' start.
	await app.send("clickWindow", { x: 18, y: 18 });

	return app.waitFor(
		"the window closed",
		(state) =>
			!state.window.visible &&
			!state.nativeWindows.some((window) => window.role === "main"),
		10_000,
	);
}

const viewsWindow: Scenario = async (context) => {
	const app = await launchPane(context);
	const log: Record<string, unknown> = {};
	const empty = await app.state();

	expect(
		empty.profiles.length === 0 && empty.window.title === "Pane",
		`a first launch: ${JSON.stringify({ profiles: empty.profiles.length, title: empty.window.title })}`,
	);

	await app.render("window-empty.png", 0);

	// ⌘T from the empty state: the toolbar comes with the tab, its address
	// field taking the keys in the same turn, before the tab's page is made.
	// An address entered at once makes the page there, not at the start page
	// (off the fixtures).
	const work = await app.createProfile("Work");

	const { state: newTab } = await app.send("menu", {
		path: ["File", "New Tab"],
	});

	const url = context.fixtures.url("nav-a.html?window");
	await app.send("navigate", { input: url });

	expect(
		newTab.activeTabID !== null && newTab.focus === "pane.toolbar.address",
		`⌘T from the empty state: ${JSON.stringify({ tab: newTab.activeTabID, focus: newTab.focus })}`,
	);

	const tab = newTab.activeTabID ?? "";
	await app.page(tab, url);

	await app.waitFor(
		"the title of the page shown",
		(state) => state.window.title === "Navigation A",
	);

	await app.render("window-page.png");

	// The page's card under the toolbar's 43 points, in the panel, and its
	// layer's corners, which the render leaves out with the page: the panel's
	// 18 as a circle of 0.57 of it (ADR 0057), all four, as the Swift's.
	type Layout = { page: number[]; corners: number[] };

	const { result: layout } = await app.send<Layout>("pageLayout");

	expect(
		JSON.stringify(layout.page) === JSON.stringify([260, 51, 1012, 741]) &&
			layout.corners.every((corner) => Math.abs(corner - 18 * 0.57) < 0.01),
		`the page's card: ${JSON.stringify(layout)}`,
	);

	log.pageLayout = layout;

	// Closed: the page goes with the window, the tab stays, still selected.
	const closed = await closeWindow(app);

	expect(
		tabOf(closed, tab)?.url === url &&
			tabOf(closed, tab)?.loaded === false &&
			closed.activeTabID === tab,
		`the window's close: ${JSON.stringify({ tab: tabOf(closed, tab), active: closed.activeTabID })}`,
	);

	// Opened again, as from the Dock: a new window, the tab's page again.
	await handOver(context, app, []);

	const reopened = await app.waitFor(
		"a new window with the tab's page",
		(state) => state.window.visible && tabOf(state, tab)?.loaded === true,
		15_000,
	);

	expect(
		trafficLightsInPlace(reopened.window.trafficLightFrames) &&
			reopened.activeTabID === tab,
		`the window opened again: ${JSON.stringify({ lights: reopened.window.trafficLightFrames, active: reopened.activeTabID })}`,
	);

	await app.page(tab, url);

	await app.waitFor(
		"its title",
		(state) => state.window.title === "Navigation A",
	);

	log.reopened = { title: reopened.window.title, frame: reopened.window.frame };

	// Closed again, then handed a URL: it opens in a new window, selected.
	await closeWindow(app);
	const handed = context.fixtures.url("nav-b.html?window");
	await handOver(context, app, [handed]);

	const opened = await app.waitFor(
		"the URL in a new window",
		(state) => {
			const tabs =
				state.profiles.find((profile) => profile.id === work)?.tabs ?? [];

			return (
				state.window.visible &&
				tabs.at(-1)?.url === handed &&
				tabs.at(-1)?.loaded === true &&
				state.activeTabID === tabs.at(-1)?.id
			);
		},
		15_000,
	);

	// Unless the runner lets it take the front, it never does (ADR 0012).
	expect(
		opened.nativeWindows.filter((window) => window.role === "main").length ===
			1 &&
			(context.foreground || !opened.window.key),
		`one window, in the background: ${JSON.stringify({ windows: opened.nativeWindows, key: opened.window.key })}`,
	);

	await app.quit();

	return log;
};

export default viewsWindow;
