import { launchPane, type PaneApp, setUpPin, tabOf } from "../lib/pane";
import { PRIMARY } from "../lib/platform";
import { waitForExit } from "../lib/processes";
import { expect, type Scenario } from "../run";

// H7, keys and menus (docs/plans/views-migration.md, Phase 3), on the Views
// build, in the background. The menu bar as it shows, item for item against
// the Swift's MainMenu, and the enabled states its validation gave, behind
// the Lock too; items chosen through the real menu bar, as a click chooses
// them; Quit Pane? asked by ⌘Q, a key posted through AppKit's queue, which
// the menu bar takes as it takes the user's, with Views' own dispatch of the
// keys it then holds, and its renders, for the Swift's surfaces-quit-confirm;
// and the switcher opened by Ctrl-Tab and chosen by Ctrl's release, rendered
// under Ctrl, for the Swift's shortcuts switcher. For the Views build, from
// out/Release:
//
//   bun tests/run.ts --binary ~/chromium/src/out/Release/Pane.app/Contents/MacOS/Pane \
//     --out <dir> [--appearance light|dark] tests/rigs/views-menus.ts

type Item = {
	title: string;
	key: string;
	enabled: boolean;
	hidden: boolean;
	checked: boolean;
	submenu?: Item[];
};

// The Swift's MainMenu: each menu's items, title and key, as they showed.
const SWIFT: Record<string, string[]> = {
	Pane: [
		"About Pane",
		"Check for Updates…",
		"Settings… ⌘,",
		"Lock Pane ⌃⌘L",
		"Hide Pane ⌘H",
		"Hide Others ⌥⌘H",
		"Show All",
		"Quit Pane ⌘Q",
	],
	File: ["New Tab ⌘T", "Close Tab ⌘W", "Reopen Closed Tab ⇧⌘T", "Print… ⌘P"],
	Edit: [
		"Undo ⌘Z",
		"Redo ⇧⌘Z",
		"Cut ⌘X",
		"Copy ⌘C",
		"Paste ⌘V",
		"Select All ⌘A",
	],
	View: [
		"Find in Page ⌘F",
		// Chrome's and Safari's, which the Swift lacked (the user's call).
		"Find Next ⌘G",
		"Find Previous ⇧⌘G",
		"Reload ⌘R",
		"Actual Size ⌘0",
		"Zoom In ⌘+",
		"Zoom Out ⌘-",
		"Enter Full Screen ⌃⌘F",
		"Developer Tools ⇧⌘I",
	],
	Navigate: ["Back ⌘[", "Forward ⌘]", "Focus Address Bar ⌘L"],
	Tab: [
		"Next Tab (Recent) ⌃⇥",
		"Previous Tab (Recent) ⌃⇧⇥",
		...Array.from(
			{ length: 8 },
			(_, index) => `Tab ${index + 1} ⌘${index + 1}`,
		),
		// The Swift's Tab 9, now the last tab, as Chrome's and Safari's (the
		// user's call).
		"Last Tab ⌘9",
	],
	Window: ["Minimize ⌘M", "Zoom", "Extensions"],
};

// The items the app runs, which its validation enables; the rest are
// AppKit's (About, Hide, Show All, the Edit menu, Minimize, Zoom), which the
// responder chain enables, and AppKit's own additions.
const PLATFORM = new Set([
	"About Pane",
	"Hide Pane",
	"Hide Others",
	"Show All",
	"Undo",
	"Redo",
	"Cut",
	"Copy",
	"Paste",
	"Select All",
	"Minimize",
	"Zoom",
]);

// Behind the Lock, the Swift's allowedWhileLocked.
const WHILE_LOCKED = new Set(["Check for Updates…", "Quit Pane"]);

const line = (item: Item) =>
	item.key ? `${item.title} ${item.key}` : item.title;

async function menuBar(app: PaneApp): Promise<Item[]> {
	return (await app.send<Item[]>("menuBar")).result;
}

// The app's items of each menu, as they show, in their order: what AppKit
// adds (the Edit menu's dictation, the Window menu's windows) is left out.
function appItems(bar: Item[]): Record<string, Item[]> {
	return Object.fromEntries(
		bar.map((menu) => [
			menu.title,
			(menu.submenu ?? []).filter(
				(item) =>
					!item.hidden &&
					(SWIFT[menu.title] ?? []).some(
						(swift) =>
							swift === line(item) || swift.startsWith(`${item.title} `),
					),
			),
		]),
	);
}

function enabledOf(bar: Item[]): Record<string, boolean> {
	const enabled: Record<string, boolean> = {};

	for (const items of Object.values(appItems(bar))) {
		for (const item of items) {
			if (!PLATFORM.has(item.title)) {
				enabled[item.title] = item.enabled;
			}
		}
	}

	return enabled;
}

const viewsMenus: Scenario = async (context) => {
	const { fixtures } = context;
	const app = await launchPane(context);

	// Item for item, as the Swift's showed them; ⌘= is Zoom In's hidden
	// twin, and ⌥⌘I Developer Tools', their keys kept.
	const bar = await menuBar(app);

	expect(
		JSON.stringify(bar.map((menu) => menu.title)) ===
			JSON.stringify(Object.keys(SWIFT)),
		`the menus: ${JSON.stringify(bar.map((menu) => menu.title))}`,
	);

	const shown = appItems(bar);

	for (const [title, swift] of Object.entries(SWIFT)) {
		const lines = (shown[title] ?? []).map(line);

		expect(
			JSON.stringify(lines) === JSON.stringify(swift),
			`${title}: ${JSON.stringify(lines)}`,
		);
	}

	const hidden = (
		bar
			.find((menu) => menu.title === "View")
			?.submenu?.filter((item) => item.hidden) ?? []
	).map(line);

	expect(
		JSON.stringify(hidden) ===
			JSON.stringify(["Zoom In ⌘=", "Developer Tools ⌥⌘I"]),
		`View's hidden items: ${JSON.stringify(hidden)}`,
	);

	// Every item of the app's enabled once the profiles are open, but Lock
	// Pane, which needs a PIN.
	const open = enabledOf(bar);

	expect(
		Object.entries(open).every(
			([title, enabled]) => enabled === (title !== "Lock Pane"),
		),
		`enabled with the profiles open: ${JSON.stringify(open)}`,
	);

	// Items chosen through the menu bar. Tab 2 is the second tab; Last Tab
	// (⌘9), with three, the third, as Chrome's and Safari's.
	const profile = await app.createProfile("Menus", "BLUE");
	const tabs: string[] = [];

	for (const name of ["one", "two", "three"]) {
		tabs.push(
			(await app.openPage(profile, fixtures.url(`nav-a.html?${name}`))).tab,
		);
	}

	const choose = async (path: string[]) =>
		(await app.send<boolean>("menu", { path })).result;

	expect(await choose(["Tab", "Tab 2"]), "Tab 2 did not run");
	await app.waitFor("Tab 2", (state) => state.activeTabID === tabs[1]);
	expect(await choose(["Tab", "Last Tab"]), "Last Tab did not run");
	await app.waitFor("Last Tab", (state) => state.activeTabID === tabs[2]);

	// Next Tab (Recent) opens the switcher on the tab used before; Previous
	// moves its selection back; Ctrl's release, through Views' dispatch,
	// chooses it.
	expect(await choose(["Tab", "Next Tab (Recent)"]), "Next Tab did not run");

	const switcher = await app.waitFor(
		"the switcher",
		(state) => state.switcher.visible,
	);

	expect(
		await choose(["Tab", "Previous Tab (Recent)"]),
		"Previous Tab did not run",
	);

	await app.waitFor(
		"the switcher's selection back",
		(state) => state.switcher.selectedIndex !== switcher.switcher.selectedIndex,
	);

	await app.send("dispatchKey", { key: "control", phase: "release" });

	await app.waitFor(
		"the switcher closed by Ctrl's release",
		(state) => !state.switcher.visible,
	);

	// View › Enter Full Screen's title follows the window (the full screen
	// itself takes the front: fullscreen).
	const view = (await menuBar(app)).find((menu) => menu.title === "View");

	expect(
		view?.submenu?.some((item) => item.title === "Enter Full Screen") === true,
		"View has no Enter Full Screen",
	);

	// Quit Pane?: ⌘Q from the page, posted through AppKit's queue.
	await app.send("clickPage", { x: 20, y: 20 });

	await app.waitFor(
		"the page focused",
		(state) => state.focus === "pane.content",
	);

	await app.send("key", { key: "q", modifiers: [PRIMARY] });

	await app.waitFor(
		"Quit Pane?",
		(state) => state.quitConfirmation && state.focus === "pane.quit",
	);

	await app.render("quit-confirm.png", 600);

	// It takes every key: the window's commands, a letter, and Tab stop
	// there, and the menu bar still offers its items.
	const before = (await app.state()).profiles.find((p) => p.id === profile)
		?.tabs.length;

	for (const key of [
		{ key: "t", modifiers: [PRIMARY] },
		{ key: "a" },
		{ key: "\t" },
	]) {
		await app.send("dispatchKey", key);
	}

	await Bun.sleep(300);
	const held = await app.state();

	expect(
		held.quitConfirmation &&
			held.focus === "pane.quit" &&
			held.profiles.find((p) => p.id === profile)?.tabs.length === before,
		`keys reached past Quit Pane?: ${JSON.stringify({ focus: held.focus, before })}`,
	);

	const whileAsking = enabledOf(await menuBar(app));

	expect(
		Object.entries(whileAsking).every(
			([title, enabled]) => enabled === (title !== "Lock Pane"),
		),
		`the menu bar while Quit Pane? asks: ${JSON.stringify(whileAsking)}`,
	);

	// Esc goes back, the page focused again.
	await app.send("dispatchKey", { key: "\u001b" });

	await app.waitFor(
		"Quit Pane? gone, the page focused",
		(state) => !state.quitConfirmation && state.focus === "pane.content",
	);

	// Cancel, and a click outside it, go back too.
	for (const press of ["pane.quit.cancel", "scrim"]) {
		await app.send("key", { key: "q", modifiers: [PRIMARY] });
		await app.waitFor("Quit Pane? again", (state) => state.quitConfirmation);
		await Bun.sleep(400);

		if (press === "scrim") {
			await app.send("clickWindow", { x: 120, y: 700 });
		} else {
			await app.click(press);
		}

		await app.waitFor(
			`Quit Pane? gone by ${press}`,
			(state) => !state.quitConfirmation,
		);
	}

	// The switcher under Ctrl, as shortcuts' render: one profile of two
	// tabs, the other selected; Ctrl-Tab posted through AppKit's queue.
	await app.send("unloadProfile", { profile });
	const keys = await app.createProfile("Keys", "BLUE");
	await app.openPage(keys, fixtures.url("nav-b.html"));
	const last = (await app.openPage(keys, fixtures.url("nav-a.html?keys"))).tab;

	await app.waitFor("Navigation A's favicon", (state) => {
		const tab = tabOf(state, last);

		return tab?.hasFavicon === true && state.activeTabID === last;
	});

	await app.send("key", { key: "\t", modifiers: ["control"] });

	const underCtrl = await app.waitFor(
		"the switcher by Ctrl-Tab",
		(state) => state.switcher.visible,
	);

	await app.render("switcher-under-ctrl.png");

	// Ctrl-Tab again moves the selection on, while Ctrl is down.
	await app.send("key", { key: "\t", modifiers: ["control"] });

	await app.waitFor(
		"the selection moved on",
		(state) =>
			state.switcher.selectedIndex !== underCtrl.switcher.selectedIndex,
	);

	// Esc with Ctrl down closes it, choosing nothing.
	await app.send("dispatchKey", { key: "\u001b", modifiers: ["control"] });

	const cancelled = await app.waitFor(
		"the switcher closed by Esc",
		(state) => !state.switcher.visible,
	);

	expect(cancelled.activeTabID === last, "Esc in the switcher chose a tab");

	// Behind the Lock: the app's items disabled but Check for Updates… and
	// Quit Pane, as the Swift's validation; a choice of one does nothing.
	await setUpPin(app, "2468");
	const withPin = enabledOf(await menuBar(app));

	expect(withPin["Lock Pane"] === true, "Lock Pane disabled with a PIN");
	expect(await choose(["Pane", "Lock Pane"]), "Lock Pane did not run");
	await app.waitFor("the Lock", (state) => state.security.locked);
	const locked = enabledOf(await menuBar(app));

	expect(
		Object.entries(locked).every(
			([title, enabled]) => enabled === WHILE_LOCKED.has(title),
		),
		`behind the Lock: ${JSON.stringify(locked)}`,
	);

	expect(
		(await choose(["File", "New Tab"])) === false,
		"New Tab ran behind the Lock",
	);

	// ⌘Q at the PIN screen quits at once: nothing is open to lose there.
	await app.send("key", { key: "q", modifiers: [PRIMARY] });

	expect(
		await waitForExit(app.browser.pid),
		"⌘Q at the PIN screen did not quit",
	);

	return { bar, appearance: context.appearance ?? "system" };
};

export default viewsMenus;
