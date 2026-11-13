import { join } from "node:path";
import { type AppState, launchPane, type PaneApp, tabOf } from "../lib/pane";
import { captureWindow } from "../lib/tools";
import { expect, type Scenario } from "../run";

// H2, the sidebar (docs/plans/views-migration.md, Phase 3), on the Views
// build, in the background. Its renders, for the Swift's (surfaces'
// window-two-profiles and sidebar-overflow-end and -middle) and Paper 03.1 to
// 03.3's states (a row under the pointer, Settings shown, the delete alert);
// and what its rows do: the favicon a page sends, drawn and saved; a tab
// without a page dimmed; a tab's X, a real click under the pointer, closing
// it; the row menus' items, checks, and icons, and what each chosen item does;
// the delete alert, the system's sheet, its Cancel keeping the profile and its
// Delete deleting it; a profile's header, clicked, collapsing it; Settings.
// For the Views build, from out/Release:
//
//   bun tests/run.ts --binary ~/chromium/src/out/Release/Pane.app/Contents/MacOS/Pane \
//     --out <dir> [--appearance light|dark] tests/rigs/views-sidebar.ts

type Frames = Record<string, number[]>;

// A tab at `url`, once loaded and titled.
async function loadTab(
	app: PaneApp,
	profile: string,
	url: string,
): Promise<string> {
	const tab = await app.openTab(profile, url);

	await app.waitFor(
		`${url} loaded`,
		(state) => {
			const shown = tabOf(state, tab);

			return shown?.url === url && !shown.loading && shown.title !== "";
		},
		30_000,
	);

	return tab;
}

function profileOf(state: AppState, id: string) {
	return state.profiles.find((profile) => profile.id === id);
}

type MenuItem = {
	title: string;
	enabled?: boolean;
	checked?: boolean;
	icon?: boolean;
};

// A menu's items as one line each: title, enabled, checked, icon.
function itemsOf(items: MenuItem[]): string[] {
	return items.map((item) =>
		item.title === "-"
			? "-"
			: `${item.title} ${item.enabled ? "on" : "off"} ${item.checked ? "checked" : "-"} ${item.icon ? "icon" : "-"}`,
	);
}

// A frame's middle, from the window's top left.
function middle(frame: number[] | undefined): { x: number; y: number } {
	const [x = 0, y = 0, width = 0, height = 0] = frame ?? [];

	return { x: x + width / 2, y: y + height / 2 };
}

const viewsSidebar: Scenario = async (context) => {
	const { fixtures } = context;
	const dataDir = context.tempDir("pane-sidebar-");
	const log: Record<string, unknown> = {};
	let app = await launchPane(context, { dataDir });

	// Five profiles, as the Swift's surfaces rig made them.
	const work = await app.createProfile("Work", "BLUE");
	const proxied = await app.createProfile("Proxied", "EMERALD");
	const refused = await app.createProfile("Wrong proxy password", "ROSE");
	const silent = await app.createProfile("Silent proxy", "AMBER");
	const personal = await app.createProfile("Personal", "VIOLET");

	const calendar = await loadTab(
		app,
		work,
		fixtures.url("switcher.html?name=Calendar"),
	);

	const docs = await loadTab(
		app,
		work,
		fixtures.url("switcher.html?name=Docs"),
	);

	const home = await loadTab(app, work, fixtures.url("nav-a.html?work"));

	const inbox = await loadTab(
		app,
		work,
		fixtures.url("switcher.html?name=Inbox"),
	);

	const through = await loadTab(
		app,
		proxied,
		fixtures.url("switcher.html?name=Through%20the%20proxy"),
	);

	for (const name of ["Notes", "Music"]) {
		await loadTab(app, personal, fixtures.url(`switcher.html?name=${name}`));
	}

	// The page's favicon, drawn as it came and saved with its tab once encoded
	// off the UI thread.
	await app.waitFor(
		"Navigation A's favicon saved",
		(state) => tabOf(state, home)?.hasFavicon === true,
		10_000,
	);

	// Relaunched, no tab has a page until it is chosen: Calendar stays asleep,
	// dimmed, and Navigation A's saved favicon draws decoded.
	await app.send("save");
	await app.browser.close();
	app = await launchPane(context, { dataDir });

	for (const tab of [docs, inbox, through, home]) {
		await app.send("select", { tab });

		await app.waitFor(
			"the tab's page",
			(state) => tabOf(state, tab)?.loaded === true,
			15_000,
		);
	}

	// The proxies' last tests, which the proxy's port (N2) runs.
	for (const [profile, status] of [
		[proxied, "connected"],
		[refused, "failed"],
		[silent, "testing"],
	] as const) {
		await app.send("setProxyStatus", { profile, status });
	}

	let state = await app.state();

	expect(
		tabOf(state, calendar)?.loaded === false &&
			profileOf(state, personal)?.expanded === false &&
			profileOf(state, work)?.expanded === true,
		`the sidebar's profiles: ${JSON.stringify(state.profiles.map((profile) => ({ name: profile.name, expanded: profile.expanded })))}`,
	);

	await app.render("sidebar-window-two-profiles.png", 800);
	let frames: Frames = await app.frames();
	log.frames = frames;

	// Under the pointer: a tab's row lights and shows its X; a profile's
	// header lights, its text unchanged; New keeps its muted text.
	await app.send("hoverWindow", middle(frames[`pane.tab.${docs}`]));
	await app.render("sidebar-hover-tab.png", 500);
	await app.send("hoverWindow", middle(frames[`pane.profile.${personal}`]));
	await app.render("sidebar-hover-profile.png", 500);
	await app.send("hoverWindow", middle(frames["pane.sidebar.new"]));
	await app.render("sidebar-hover-new.png", 500);
	await app.send("hoverWindow", { x: 700, y: 400 });

	// A profile's menu, the Mac's own (Paper 03.1): its items as its model
	// gives them, the icons with them.
	const profileMenu = itemsOf(
		(await app.send<MenuItem[]>("rowMenu", { profile: work })).result,
	);

	log.profileMenu = profileMenu;

	expect(
		JSON.stringify(profileMenu) ===
			JSON.stringify([
				"Edit profile on - icon",
				"Keep loaded on - icon",
				"-",
				"Delete profile on - icon",
			]),
		`Work's menu: ${JSON.stringify(profileMenu)}`,
	);

	await app.send("rowMenu", { profile: work, choose: "Keep loaded" });

	await app.waitFor(
		"Work kept loaded",
		(shown) => profileOf(shown, work)?.keepLoaded === true,
	);

	const checked = itemsOf(
		(await app.send<MenuItem[]>("rowMenu", { profile: work })).result,
	);

	expect(
		checked.includes("Keep loaded on checked icon"),
		`Keep loaded, chosen, checks: ${JSON.stringify(checked)}`,
	);

	await app.send("rowMenu", { profile: work, choose: "Keep loaded" });

	// A tab's menu: Close tab closes it.
	const tabMenu = itemsOf(
		(await app.send<MenuItem[]>("rowMenu", { tab: inbox })).result,
	);

	log.tabMenu = tabMenu;

	expect(
		JSON.stringify(tabMenu) === JSON.stringify(["Close tab on - -"]),
		`a tab's menu: ${JSON.stringify(tabMenu)}`,
	);

	await app.send("rowMenu", { tab: inbox, choose: "Close tab" });
	await app.waitFor("Inbox closed", (shown) => !tabOf(shown, inbox));

	// A tab's X, a real click under the pointer: Calendar, asleep, goes at
	// once.
	frames = await app.frames();

	const [rowX = 0, rowY = 0, rowWidth = 0, rowHeight = 0] =
		frames[`pane.tab.${calendar}`] ?? [];

	const x = { x: rowX + rowWidth - 8 - 7, y: rowY + rowHeight / 2 };
	await app.send("hoverWindow", x);
	const hit = await app.hitTest(x.x, x.y);
	log.closeHit = hit;
	await app.send("clickWindow", x);

	await app.waitFor(
		"Calendar closed by its X",
		(shown) => !tabOf(shown, calendar),
		5_000,
	);

	// More rows than the sidebar shows: scrolled to the last tab opened, which
	// fades the list's top edge, then a little back, which fades both.
	await app.send("setProxyStatus", { profile: silent, status: "failed" });
	const reading = await app.createProfile("Reading list", "TEAL");

	for (let article = 1; article <= 12; article++) {
		await loadTab(
			app,
			reading,
			fixtures.url(`switcher.html?name=Article%20${article}`),
		);
	}

	await app.send("hoverWindow", { x: 700, y: 400 });

	// The edge dragged with the list full: each step timed from its event to
	// the window's layout at the new width (ADR 0061 measured the Swift's at
	// 3.4 to 4.8 ms at the median with 25 tabs), then back.
	const steps: number[] = [];

	for (const [from, to] of [
		[258, 338],
		[338, 258],
	] as const) {
		const { result } = await app.send<{ stepMs: number[] }>("dragWindow", {
			from: [from, 600],
			to: [to, 600],
			steps: 40,
		});

		steps.push(...result.stepMs);
	}

	steps.sort((a, b) => a - b);

	log.resizeSteps = {
		median: steps[Math.floor(steps.length / 2)],
		p95: steps[Math.floor(steps.length * 0.95)],
		max: steps.at(-1),
	};

	expect(
		(await app.state()).sidebarWidth === 260,
		"the edge dragged out and back is not at 260",
	);

	await app.render("sidebar-overflow-end.png", 900);
	frames = await app.frames();
	log.overflowEnd = frames;
	await app.send("scrollWindow", { x: 130, y: 400, dy: 160 });
	await app.render("sidebar-overflow-middle.png", 900);
	log.overflowMiddle = await app.frames();
	await app.send("unloadProfile", { profile: reading });

	// Settings: its row selected at once, and no profile shown as active,
	// Work's square back to its bar (ADR 0064); the page leaves the panel
	// (ADR 0073), and comes back with the browser.
	await app.send("select", { tab: home });

	await app.waitFor(
		"Navigation A's page",
		(shown) => tabOf(shown, home)?.loaded === true,
	);

	const panel = { x: 800, y: 500 };
	const onBrowser = (await app.hitTest(panel.x, panel.y)).target;
	await app.send("clickWindow", middle(frames["pane.sidebar.settings"]));
	await app.waitFor("Settings", (shown) => shown.page === "settings");
	const onSettings = (await app.hitTest(panel.x, panel.y)).target;
	await app.render("sidebar-settings.png", 800);
	await app.send("clickWindow", middle(frames["pane.sidebar.settings"]));
	await app.waitFor("the browser again", (shown) => shown.page === "browser");
	const back = (await app.hitTest(panel.x, panel.y)).target;

	expect(
		onBrowser === "page" && onSettings !== "page" && back === "page",
		`the page under Settings: ${onBrowser}, ${onSettings}, ${back}`,
	);

	// A profile's header, clicked: it collapses, its pages going, its tabs
	// kept; clicked again, it opens.
	frames = await app.frames();
	await app.send("clickWindow", middle(frames[`pane.profile.${proxied}`]));

	state = await app.waitFor(
		"Proxied collapsed",
		(shown) => profileOf(shown, proxied)?.expanded === false,
	);

	expect(
		tabOf(state, through)?.loaded === false,
		"collapsing Proxied left its page",
	);

	// Delete profile asks first, the system's alert on the window (Paper
	// 03.1): Cancel keeps the profile.
	await app.send("rowMenu", { profile: refused, choose: "Delete profile" });

	state = await app.waitFor(
		"the delete alert",
		(shown) => (shown.alert?.buttons.length ?? 0) > 0,
	);

	log.alert = state.alert;

	expect(
		state.alert?.title === "Delete “Wrong proxy password”?" &&
			state.alert.message === "This action cannot be undone." &&
			JSON.stringify(state.alert.buttons) ===
				JSON.stringify(["Cancel", "Delete"]),
		`the alert: ${JSON.stringify(state.alert)}`,
	);

	// The window with its sheet, as the screen shows it: the test copy's own
	// window, by its number.
	const main = state.nativeWindows.find((window) => window.role === "main");

	if (main?.number) {
		await Bun.sleep(600);

		await captureWindow(
			main.number,
			join(context.out, "sidebar-delete-alert.png"),
		);
	}

	await app.send("clickModalButton", { title: "Cancel" });

	state = await app.waitFor(
		"the alert gone",
		(shown) => !shown.alert && !!profileOf(shown, refused),
	);

	// Delete deletes it: its row goes, and the engine confirms.
	await app.send("rowMenu", { profile: refused, choose: "Delete profile" });
	await app.waitFor("the alert again", (shown) => !!shown.alert);
	await app.send("clickModalButton", { title: "Delete" });

	await app.waitFor(
		"Wrong proxy password deleted",
		(shown) =>
			!profileOf(shown, refused) && shown.pendingProfileDeletions === 0,
		20_000,
	);

	// Collapse all: every profile collapses.
	frames = await app.frames();
	await app.send("clickWindow", middle(frames["pane.sidebar.collapseAll"]));

	state = await app.waitFor("every profile collapsed", (shown) =>
		shown.profiles.every((profile) => !profile.expanded),
	);

	await app.render("sidebar-collapsed.png", 800);

	// New: the profile sheet, its name taking the keys.
	frames = await app.frames();
	await app.send("clickWindow", middle(frames["pane.sidebar.new"]));

	await app.waitFor(
		"the profile sheet",
		(shown) =>
			shown.profileSheet === "new" && shown.focus === "pane.profile.name",
		5_000,
	);

	await app.send("closeProfileSheet");

	await app.waitFor(
		"the profile sheet closed",
		(shown) => shown.profileSheet === null,
		5_000,
	);

	await Bun.write(
		join(context.out, "views-sidebar-log.json"),
		JSON.stringify(log, null, 2),
	);
};

export default viewsSidebar;
