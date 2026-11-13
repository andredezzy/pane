import { join } from "node:path";
import { frameTimes, percentiles } from "../lib/benchmark";
import { type AppState, type PaneApp, tabOf } from "../lib/pane";
import { PRIMARY } from "../lib/platform";
import { poll } from "../lib/poll";
import { signpostFrameIntervals } from "../lib/signposts";
import { waitForTarget } from "../lib/targets";
import { captureWindow } from "../lib/tools";
import { clickAt, launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// What Chrome asks for, second half (docs/plans/views-migration.md, Phase 3),
// on the Views build, in the background:
// - find in page's bar drawn as `surfaces-find-<state>.png`, the names
//   tests/rigs/surfaces.ts gave the Swift's renders (matches, no match), and
//   the states those miss (empty, a count of one digit and of two); its
//   count holding still as it counts (Paper 10.6); what VoiceOver reads of
//   it (ADR 0071); Esc giving the page the keys back;
// - the page's context menu, Chrome's own (U2), on a link, plain text, a
//   field, and selected text, with what Pane hides gone, and how long it
//   takes to make, as the Swift's engine timed it (madeMs);
// - DevTools docked beside the page, the page and DevTools sharing its place
//   (ADR 0024), and a capture of the window with them;
// - the full screen's hint (ADR 0025), drawn as Chrome's hook shows it;
// - timed by the app's signposts: ⌘F to the bar's first frame ("Show find
//   bar"), Return to the frame showing the count it steps to ("Find step"),
//   and ⇧⌘I to the frame showing DevTools' first paint ("Open developer
//   tools").
//
//   bun tests/run.ts --binary ~/chromium/src/out/Release/Pane.app/Contents/MacOS/Pane \
//     --out <dir>/light --appearance light tests/rigs/views-browsing.ts

const ROUNDS = 20;

type Point = { x: number; y: number };

async function render(app: PaneApp, name: string) {
	await app.render(`surfaces-${name}.png`, 400);
}

const viewsBrowsing: Scenario = async (context) => {
	const { fixtures } = context;
	const { app, trace } = await launchTraced(context, "views-browsing");
	const work = await app.createProfile("Work", "BLUE");
	const result: Record<string, unknown> = {};

	// Find in page: ⌘F through the menu bar, as a key posted to the app.
	const { tab: findTab } = await app.openPage(work, fixtures.url("find.html"));

	await app.send("key", { key: "f", modifiers: [PRIMARY] });

	await app.waitFor(
		"⌘F's bar, its field taking the keys",
		(state) => state.find.visible && state.focus === "pane.find.field",
	);

	await render(app, "find-empty");
	result.findAccessibility = (await app.send("findAccessibility")).result;
	result.findFrames = await app.frames("pane.find");

	// Closed, then found from the channel, which opens it as the Swift's
	// surfaces rig did: the query in the field, all of it selected.
	await app.send("dispatchKey", { key: "\u001b" });
	await app.waitFor("the bar closed", (state) => !state.find.visible);
	await app.send("find", { text: "needle" });

	await app.waitFor(
		"5 matches",
		(state) => state.find.matches === 5 && state.find.activeMatch === 1,
	);

	await render(app, "find-matches");

	// Return in the field steps on, Shift-Return back: the field's own keys.
	await app.send("dispatchKey", { key: "\r" });
	await app.waitFor("Return: match 2", (state) => state.find.activeMatch === 2);
	await app.send("dispatchKey", { key: "\r", modifiers: ["shift"] });

	await app.waitFor(
		"Shift-Return: match 1",
		(state) => state.find.activeMatch === 1,
	);

	// A count of one digit and of two: each as wide as the others of as many
	// digits, the field ending where it did.
	await app.send("find", { text: "e" });

	const counted = await app.waitFor(
		"e's matches",
		(state) => state.find.matches >= 12,
	);

	const widths: Record<string, number[]> = {};
	const rendered = new Set<number>();

	// Return through the matches, from where the search began (after the last
	// match found, as Chrome begins one), round past 9 to 10.
	for (let step = 0; step < counted.find.matches; step++) {
		const { find } = await app.state();
		const frames = await app.frames("pane.find");
		const [, , countWidth = 0] = frames["pane.find.count"] ?? [];
		const [fieldX = 0, , fieldWidth = 0] = frames["pane.find.field"] ?? [];
		const digits = String(find.activeMatch).length;

		widths[`${digits}`] ??= [];
		widths[`${digits}`]?.push(countWidth);
		widths[`field${digits}`] ??= [];
		widths[`field${digits}`]?.push(fieldX + fieldWidth);

		if (
			(find.activeMatch === 9 || find.activeMatch === 10) &&
			!rendered.has(find.activeMatch)
		) {
			rendered.add(find.activeMatch);
			await render(app, `find-count-${find.activeMatch}`);
		}

		await app.send("dispatchKey", { key: "\r" });

		await app.waitFor(
			`the match after ${find.activeMatch}`,
			(current) => current.find.activeMatch !== find.activeMatch,
		);
	}

	expect(
		widths["1"] !== undefined && widths["2"] !== undefined,
		`counts of one and two digits: ${JSON.stringify(widths)}`,
	);

	for (const [digits, values] of Object.entries(widths)) {
		expect(
			new Set(values).size === 1,
			`${digits}: the count moved as it counted, ${JSON.stringify(values)}`,
		);
	}

	result.countWidths = widths;

	await app.send("find", { text: "absent-word" });

	await app.waitFor(
		"no match",
		(state) => state.find.visible && state.find.matches === 0,
	);

	await render(app, "find-no-match");

	// Esc in the field closes the bar and gives the page the keys; the field
	// opens empty again.
	await app.send("dispatchKey", { key: "\u001b" });

	await app.waitFor(
		"the bar closed, the page with the keys",
		(state) => !state.find.visible && state.focus === "pane.content",
	);

	await app.send("key", { key: "f", modifiers: [PRIMARY] });

	const reopened = await app.waitFor("⌘F again", (state) => state.find.visible);

	expect(
		reopened.find.text === "",
		`the bar opened with ${JSON.stringify(reopened.find.text)}`,
	);

	// A press anywhere else in the window closes it, and goes nowhere else.
	const sidebarRow = (await app.frames(`pane.tab.${findTab}`))[
		`pane.tab.${findTab}`
	];

	expect(sidebarRow !== undefined, "no row for the find tab");

	const [rowX = 0, rowY = 0] = sidebarRow ?? [];
	const shieldHit = await app.hitTest(rowX + 30, rowY + 10);
	await app.send("clickWindow", { x: rowX + 30, y: rowY + 10 });

	await app.waitFor(
		"a press elsewhere closed it",
		(state) => !state.find.visible,
	);

	result.findShield = shieldHit;

	// The page's context menu, Chrome's own: its items on a link, plain text,
	// a field, and a selection, and how long each took to make.
	const { page: menuPage } = await app.openPage(
		work,
		fixtures.url("context.html"),
	);

	const send = (method: string, params?: Record<string, unknown>) =>
		menuPage.send(method, params);

	// In front, the system's menu shows and runs its own loop: Esc, a real
	// key, closes it before the next.
	const menuAt = async (point: Point, what: string) => {
		const before = (await app.state()).contextMenusShown;
		await clickAt(send, point.x, point.y, "right");

		const state = await app.waitFor(
			`the menu on ${what}`,
			(current) =>
				current.contextMenusShown > before && current.contextMenu !== null,
		);

		if (context.foreground) {
			await Bun.sleep(150);
			await app.send("key", { key: "\u001b" });

			await app.waitFor(
				"the menu closed",
				(current) => current.contextMenu === null,
			);
		}

		return state.contextMenu ?? [];
	};

	const menus: Record<string, unknown> = {};

	menus.link = await menuAt(
		await menuPage.evaluate<Point>("centerOf('link')"),
		"a link",
	);

	menus.plain = await menuAt(
		await menuPage.evaluate<Point>("centerOf('plain')"),
		"plain text",
	);

	await menuPage.evaluate(
		"(() => { const field = document.getElementById('text'); field.focus(); })()",
	);

	menus.field = await menuAt(
		await menuPage.evaluate<Point>("centerOf('text')"),
		"a field",
	);

	await menuPage.evaluate(
		"(() => { document.activeElement.blur(); const range = document.createRange(); range.selectNodeContents(document.getElementById('plain')); getSelection().removeAllRanges(); getSelection().addRange(range); })()",
	);

	menus.selection = await menuAt(
		await menuPage.evaluate<Point>("centerOf('plain')"),
		"selected text",
	);

	result.menus = menus;

	// Nothing Pane hides shows, at any depth.
	const hidden = [
		"Open Link in New Window",
		"Open Link in Split View",
		"Open Link in Incognito Window",
		"Open in Reading Mode",
		"Create QR Code for this Page",
		"Language Settings",
	];

	const titles = (items: unknown): string[] =>
		(items as { title: string; submenu?: unknown }[]).flatMap((item) => [
			item.title,
			...(item.submenu ? titles(item.submenu) : []),
		]);

	const shownHidden = Object.values(menus)
		.flatMap(titles)
		.filter(
			(title) =>
				hidden.includes(title) ||
				title.startsWith("Open Link as") ||
				title.includes("Image Search") ||
				title.includes("Google Lens"),
		);

	expect(shownHidden.length === 0, `hidden items show: ${shownHidden}`);

	// Made again and again on plain text, as the Swift's engine timed it; in
	// front, timed to the system's menu tracking too ("Context menu").
	await menuPage.evaluate("getSelection().removeAllRanges()");
	const plain = await menuPage.evaluate<Point>("centerOf('plain')");

	const menuIntervals = await signpostFrameIntervals(async () => {
		for (let round = 0; round < ROUNDS; round++) {
			await menuAt(plain, "plain text, again");
		}

		return app.browser.pid;
	});

	if (context.foreground) {
		result.contextMenuToTracking = percentiles(
			(menuIntervals["Context menu"] ?? []).map((value) => value.ms),
		);
	}

	const made = trace().filter(
		(entry) => entry.name === "PaneWebContentsDelegate.showContextMenu",
	);

	result.contextMenuTimes = {
		madeMs: percentiles(made.map((entry) => Number(entry.args.madeMs))),
		readyMs: percentiles(made.map((entry) => Number(entry.args.readyMs))),
	};

	// DevTools docked beside the page, by ⇧⌘I, as a key posted to the app.
	const { tab: inspected } = await app.openPage(
		work,
		fixtures.url("nav-a.html?devtools"),
	);

	await app.send("key", { key: "i", modifiers: [PRIMARY, "shift"] });

	await app.waitFor("DevTools", (state) =>
		state.devToolsTabs.includes(inspected),
	);

	await waitForTarget(app.browser.cdp, "the DevTools frontend", (target) =>
		target.url.startsWith("devtools://"),
	);

	const docked = await poll(
		"DevTools docked in the page's place",
		async () => {
			const frames = await app.frames("pane.");

			return frames["pane.devtools"] && frames["pane.content"]
				? frames
				: undefined;
		},
		10_000,
	);

	const [devX = 0, devY = 0, devWidth = 0, devHeight = 0] =
		docked["pane.devtools"] ?? [];

	const [pageX = 0, pageY = 0, pageWidth = 0, pageHeight = 0] =
		docked["pane.content"] ?? [];

	expect(
		pageX >= devX &&
			pageY >= devY &&
			pageX + pageWidth <= devX + devWidth &&
			pageY + pageHeight <= devY + devHeight &&
			pageWidth * pageHeight < devWidth * devHeight,
		`the page inside DevTools' place: ${JSON.stringify(docked)}`,
	);

	result.devtoolsFrames = {
		devtools: docked["pane.devtools"],
		page: docked["pane.content"],
	};

	await Bun.sleep(1_500);
	await render(app, "devtools-docked");

	// The window as the screen shows it, DevTools and the page with it: the
	// test copy's own window, by its number.
	const main = (await app.state()).nativeWindows.find(
		(window) => window.role === "main",
	);

	if (main?.number) {
		const capture = join(context.out, "capture-devtools-docked.png");

		await captureWindow(main.number, capture);
	}

	await app.send("key", { key: "i", modifiers: [PRIMARY, "shift"] });

	await app.waitFor(
		"DevTools closed",
		(state) =>
			!state.devToolsTabs.includes(inspected) && state.focus === "pane.content",
	);

	// The full screen's hint, as Chrome's hook shows it: drawn at once, gone
	// by its fade 3.5 s later; its text kept while the fullscreen lasts.
	await app.send("showExclusiveAccessHint", {
		hint: "Press Esc to exit full screen",
	});

	await render(app, "fullscreen-hint");

	result.hintFrame = (await app.frames("pane.exclusiveAccessHint"))[
		"pane.exclusiveAccessHint"
	];

	await Bun.sleep(4_200);
	await render(app, "fullscreen-hint-faded");

	expect(
		(await app.state()).exclusiveAccessHint === "Press Esc to exit full screen",
		"the hint's text went with its fade",
	);

	await app.send("showExclusiveAccessHint", { hint: "" });

	await app.waitFor(
		"the hint gone",
		(state) => state.exclusiveAccessHint === null,
	);

	// The first frames: ⌘F to the bar, Return to the next count, and ⇧⌘I to
	// DevTools' first paint.
	await app.send("select", { tab: findTab });
	await app.waitFor("the find page", (state) => state.activeTabID === findTab);

	const findIntervals = await signpostFrameIntervals(async () => {
		for (let round = 0; round < ROUNDS; round++) {
			await app.send("key", { key: "f", modifiers: [PRIMARY] });
			await app.waitFor("the bar", (state) => state.find.visible);
			await Bun.sleep(150);
			await app.send("dispatchKey", { key: "\u001b" });
			await app.waitFor("the bar gone", (state) => !state.find.visible);
			await Bun.sleep(150);
		}

		await app.send("find", { text: "needle" });
		await app.waitFor("needles", (state) => state.find.matches === 5);

		for (let round = 0; round < ROUNDS; round++) {
			const before = (await app.state()).find.activeMatch;
			await app.send("dispatchKey", { key: "\r" });

			await app.waitFor(
				"the next needle",
				(state: AppState) => state.find.activeMatch !== before,
			);

			await Bun.sleep(150);
		}

		await app.send("closeFind");

		for (let round = 0; round < ROUNDS / 2; round++) {
			await app.send("key", { key: "i", modifiers: [PRIMARY, "shift"] });

			await app.waitFor("DevTools", (state) =>
				state.devToolsTabs.includes(findTab),
			);

			await Bun.sleep(1_200);
			await app.send("key", { key: "i", modifiers: [PRIMARY, "shift"] });

			await app.waitFor(
				"DevTools closed",
				(state) => !state.devToolsTabs.includes(findTab),
			);

			await Bun.sleep(300);
		}

		return app.browser.pid;
	});

	const timings: Record<string, unknown> = {};

	for (const name of ["Show find bar", "Find step", "Open developer tools"]) {
		const intervals = findIntervals[name] ?? [];
		expect(intervals.length > 0, `${name}: no intervals`);
		timings[name] = frameTimes(intervals);
	}

	result.timings = timings;
	expect(tabOf(await app.state(), findTab) !== undefined, "the find tab went");

	return result;
};

export default viewsBrowsing;
