import { tabOf } from "../lib/pane";
import { poll } from "../lib/poll";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

type Key = { key: string; modifiers?: string[] };

const ADDRESS = "field: Search or enter URL";
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

	const checks: Record<string, unknown> = {};

	const profile = await app.createProfile("Keys");
	const a = context.fixtures.url("nav-a.html");
	const b = context.fixtures.url("nav-b.html");
	const tabA = await app.openTab(profile, a);
	await app.page(tabA, a);

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
	checks["⌘T"] = { tabs: opened.profiles[0]?.tabs.length, focus: opened.focus };

	// ⌘1: the profile's first tab.
	await press("1", "command");
	await app.waitFor("⌘1", (state) => state.activeTabID === tabA);

	// ⌘2, then ⌘W closes it; ⇧⌘T brings it back.
	await press("2", "command");
	await app.waitFor("⌘2", (state) => state.activeTabID === tabNew);
	await press("w", "command");

	const closed = await app.waitFor(
		"⌘W",
		(state) => state.profiles[0]?.tabs.length === 1 && state.closedTabs === 1,
	);

	checks["⌘W"] = {
		tabs: closed.profiles[0]?.tabs.length,
		active: closed.activeTabID === tabA,
	};

	await press("t", "command", "shift");

	const reopened = await app.waitFor(
		"⇧⌘T",
		(state) => state.profiles[0]?.tabs.length === 2 && state.closedTabs === 0,
	);

	checks["⇧⌘T"] = { tabs: reopened.profiles[0]?.tabs.map((tab) => tab.url) };

	await press("1", "command");
	await app.waitFor("⌘1 again", (state) => state.activeTabID === tabA);

	// ⌘F opens the find bar with its field focused; Escape closes it.
	await press("f", "command");

	const finding = await app.waitFor(
		"⌘F",
		(state) => state.find.visible && state.focus === FIND,
	);

	checks["⌘F"] = { visible: finding.find.visible, focus: finding.focus };

	await press("\u001b");
	const unfinding = await app.waitFor("Escape", (state) => !state.find.visible);
	checks.Escape = { visible: unfinding.find.visible };

	// ⌘L focuses the address field.
	await press("l", "command");

	const addressing = await app.waitFor(
		"⌘L",
		(state) => state.focus === ADDRESS,
	);

	checks["⌘L"] = { focus: addressing.focus };

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

	checks["⌘[ ⌘]"] = "back, forward, back";

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
	checks["⌘R"] = { loadsBefore: before, loadsAfter: after };

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

		const deadline = Date.now() + 5_000;

		while (zoomLevels().length === count && Date.now() < deadline) {
			await Bun.sleep(50);
		}

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

			const until = Date.now() + 5_000;
			pageRatio = await ratio();

			while (!expected(pageRatio) && Date.now() < until) {
				await Bun.sleep(100);
				pageRatio = await ratio();
			}

			expect(
				expected(pageRatio),
				`⌘${key}: page devicePixelRatio ${pageRatio} at zoom level ${entry.args.after}`,
			);
		}

		return { level: entry.args.after, pageVisible, pageRatio };
	};

	checks["⌘= ⌘0 ⌘-"] = {
		base,
		zoomIn: await zoom("=", "zoomIn", (level) => level > 0),
		reset: await zoom("0", "resetZoom", (level) => level === 0),
		zoomOut: await zoom("-", "zoomOut", (level) => level < 0),
		resetAgain: await zoom("0", "resetZoom", (level) => level === 0),
	};

	// ⌃⇥ opens the switcher on the previous tab; releasing Control picks it.
	await press("\t", "control");
	const switching = await app.waitFor("⌃⇥", (state) => state.switcher.visible);
	await app.send("releaseModifiers");

	const switched = await app.waitFor(
		"Control released",
		(state) => !state.switcher.visible,
	);

	expect(
		switched.activeTabID !== tabA,
		"the switcher did not move to the previous tab",
	);

	checks["⌃⇥"] = {
		groups: switching.switcher.groups,
		selected: switched.activeTabID,
	};

	// ⌃⇧⇥ opens it stepping backward; Escape cancels without switching.
	const activeBefore = (await app.state()).activeTabID;
	await press("\t", "control", "shift");
	const backward = await app.waitFor("⌃⇧⇥", (state) => state.switcher.visible);
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

	checks["⌃⇧⇥"] = {
		selectedIndex: backward.switcher.selectedIndex,
		cancelled: true,
	};

	// ⌘, opens settings.
	await press(",", "command");

	const settings = await app.waitFor(
		"⌘,",
		(state) => state.page === "settings",
	);

	checks["⌘,"] = { page: settings.page };

	return checks;
};

export default shortcuts;
