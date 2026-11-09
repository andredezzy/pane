import { tabOf } from "../lib/pane";
import { poll, pollValue } from "../lib/poll";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

type Key = { key: string; modifiers?: string[] };

const ADDRESS = "pane.toolbar.address";
const FIND = "field: Find in page";

const ZOOM_CALLS = new Set([
	"PaneWebContents.zoomIn",
	"PaneWebContents.zoomOut",
	"PaneWebContents.resetZoom",
]);

// Every app shortcut, sent as key events through AppKit's queue (event
// monitors, then menu key equivalents), and its effect on the app.
const shortcuts: Scenario = async (context) => {
	const { app, trace } = await launchTraced(context, "shortcuts");

	const press = (key: string, ...modifiers: string[]) =>
		app.send("key", { key, modifiers } satisfies Key);

	const profile = await app.createProfile("Keys");
	const a = context.fixtures.url("nav-a.html");
	const b = context.fixtures.url("nav-b.html");
	const { tab: tabA } = await app.openPage(profile, a);

	// ⌘T: a new tab in the active profile, with the address field focused.
	await press("t", "command");

	const opened = await app.waitFor("⌘T", (state) => {
		const tabs = state.profiles[0]?.tabs ?? [];

		return (
			tabs.length === 2 &&
			state.activeTabID === tabs[1]?.id &&
			state.focus === ADDRESS
		);
	});

	const tabNew = opened.activeTabID as string;

	// What is typed replaces the address that came after the field took the focus,
	// and ⌘L selects it again while the field has the focus.
	await app.waitFor("the new tab's address", (state) => state.address !== "");
	await Bun.sleep(100);
	await press("a");
	const typed = await app.waitFor("the key", (state) => state.address !== "");

	expect(
		typed.address === "a",
		`typing in a new tab's address field gave "${typed.address}"`,
	);

	// A key already in the queue behind ⌘L acts on what ⌘L did: it replaces the
	// text. Both land in one of the automation's polls, so no front is needed.
	await Promise.all([press("l", "command"), press("q")]);

	const queued = await app.waitFor(
		"the key queued behind ⌘L",
		(state) => state.address !== "a",
	);

	expect(
		queued.address === "q",
		`a key queued behind ⌘L in the field gave "${queued.address}"`,
	);

	await press("a");
	await app.waitFor("the field back to a", (state) => state.address !== "q");
	await press("l", "command");
	await press("a");
	await app.waitFor("the field holding a", (state) => state.address === "a");

	// Behind the app in use its window isn't key, so nothing takes the select.
	if (context.foreground) {
		await press("l", "command");
		await Bun.sleep(100);
		await press("p");

		const retyped = await app.waitFor(
			"the key after ⌘L",
			(state) => state.address !== "a",
		);

		expect(
			retyped.address === "p",
			`typing after ⌘L in the focused field gave "${retyped.address}"`,
		);
	} else {
		context.defer("⌘L selecting again: it needs Pane in front");
	}

	await press("\u001b");

	// ⌘1: the profile's first tab.
	await press("1", "command");
	await app.waitFor("⌘1", (state) => state.activeTabID === tabA);

	// ⌘2, then ⌘W closes it; ⇧⌘T brings it back.
	await press("2", "command");
	await app.waitFor("⌘2", (state) => state.activeTabID === tabNew);
	await press("w", "command");

	await app.waitFor(
		"⌘W",
		(state) => state.profiles[0]?.tabs.length === 1 && state.closedTabs === 1,
	);

	await press("t", "command", "shift");

	await app.waitFor(
		"⇧⌘T",
		(state) => state.profiles[0]?.tabs.length === 2 && state.closedTabs === 0,
	);

	await press("1", "command");
	await app.waitFor("⌘1 again", (state) => state.activeTabID === tabA);

	// ⌘F opens the find bar with its field focused; Escape closes it.
	await press("f", "command");

	await app.waitFor(
		"⌘F",
		(state) => state.find.visible && state.focus === FIND,
	);

	await press("\u001b");
	await app.waitFor("Escape", (state) => !state.find.visible);

	// ⌘L focuses the address field.
	await press("l", "command");

	await app.waitFor("⌘L", (state) => state.focus === ADDRESS);

	// ⌘[ and ⌘] walk the history.
	await app.send("navigate", { input: b });

	await app.waitFor(
		"B",
		(state) => tabOf(state, tabA)?.url === b && !tabOf(state, tabA)?.loading,
	);

	await press("[", "command");

	await app.waitFor(
		"⌘[",
		(state) => tabOf(state, tabA)?.url === a && !tabOf(state, tabA)?.loading,
	);

	await press("]", "command");

	await app.waitFor(
		"⌘]",
		(state) => tabOf(state, tabA)?.url === b && !tabOf(state, tabA)?.loading,
	);

	await press("[", "command");

	await app.waitFor(
		"⌘[ again",
		(state) => tabOf(state, tabA)?.url === a && !tabOf(state, tabA)?.loading,
	);

	// ⌘R reloads.
	const before = Number(
		await (await app.page(tabA, a)).evaluate<string>("sessionStorage.loads"),
	);

	await press("r", "command");

	// Reloaded once the page has counted another load.
	const reloaded = await app.page(tabA, a);

	const after = await poll(
		"⌘R",
		async () => {
			const loads = Number(
				await reloaded
					.evaluate<string>("sessionStorage.loads")
					.catch(() => before),
			);

			return loads !== before && loads;
		},
		10_000,
	);

	await app.waitFor("⌘R", (state) => !tabOf(state, tabA)?.loading);

	expect(after === before + 1, `⌘R loaded the page ${after - before} times`);

	// ⌘= / ⌘0 / ⌘- change the page zoom. The engine trace records each zoom
	// level; a visible page also shows it in devicePixelRatio. (While the
	// screen is locked the window is occluded, and Chromium sends an occluded
	// page no visual updates, so the page's own ratio can lag.)
	const zoomLevels = () =>
		trace()
			.filter((entry) => ZOOM_CALLS.has(entry.name))
			.map((entry) => ({
				name: entry.name,
				args: entry.args as { before: number; after: number },
			}));

	const zoomPage = await app.page(tabA, a);
	const ratio = () => zoomPage.evaluate<number>("devicePixelRatio");

	const visible = async () =>
		(await zoomPage.evaluate<string>("document.visibilityState")) === "visible";

	const base = await ratio();

	const zoom = async (
		key: string,
		name: string,
		predicate: (level: number) => boolean,
	) => {
		const count = zoomLevels().length;
		await press(key, "command");

		await pollValue(
			() => zoomLevels().length,
			(length) => length !== count,
			5_000,
			50,
		);

		const entry = zoomLevels()[count];

		expect(
			entry?.name === `PaneWebContents.${name}`,
			`⌘${key} did not reach ${name}`,
		);

		expect(
			predicate(entry.args.after),
			`⌘${key}: zoom level ${entry.args.before} → ${entry.args.after}`,
		);

		const pageVisible = await visible();
		let pageRatio: number | null = null;

		if (pageVisible) {
			// The ratio moves the way the zoom level does.
			const expected = (value: number) =>
				Math.sign(value - base) === Math.sign(entry.args.after);

			pageRatio = await pollValue(ratio, expected, 5_000);

			expect(
				expected(pageRatio),
				`⌘${key}: page devicePixelRatio ${pageRatio} at zoom level ${entry.args.after}`,
			);
		}

		return { level: entry.args.after, pageVisible, pageRatio };
	};

	await zoom("=", "zoomIn", (level) => level > 0);
	await zoom("0", "resetZoom", (level) => level === 0);
	await zoom("-", "zoomOut", (level) => level < 0);
	await zoom("0", "resetZoom", (level) => level === 0);

	// ⌃⇥ opens the switcher on the previous tab; releasing Control picks it.
	await press("\t", "control");
	await app.waitFor("⌃⇥", (state) => state.switcher.visible);
	await app.render("switcher.png");
	await app.send("releaseModifiers");

	const switched = await app.waitFor(
		"Control released",
		(state) => !state.switcher.visible,
	);

	expect(
		switched.activeTabID !== tabA,
		"the switcher did not move to the previous tab",
	);

	// ⌃⇧⇥ opens it stepping backward; Escape cancels without switching.
	const activeBefore = (await app.state()).activeTabID;
	await press("\t", "control", "shift");
	await app.waitFor("⌃⇧⇥", (state) => state.switcher.visible);
	await press("\u001b", "control");

	const cancelled = await app.waitFor(
		"Escape in the switcher",
		(state) => !state.switcher.visible,
	);

	await app.send("releaseModifiers");

	expect(
		cancelled.activeTabID === activeBefore,
		"Escape in the switcher changed the tab",
	);

	// ⌘, opens settings.
	await press(",", "command");

	await app.waitFor("⌘,", (state) => state.page === "settings");
};

export default shortcuts;
