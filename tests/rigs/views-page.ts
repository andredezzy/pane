import { join } from "node:path";
import { isScreenLocked, mainWindowOf } from "../lib/computer-use";
import { launchPane } from "../lib/pane";
import { captureWindow } from "../lib/tools";
import {
	accessibilityTree,
	activateWebAccessibility,
	describeElement,
} from "../lib/windows";
import { expect, type Scenario } from "../run";

// G3 on the Views prototype (docs/plans/views-migration.md, Phase 1): the
// page in the window's one WebView, checked as E7 checked the Swift app's
// (62 µs to hit test, a resize step in 1.55 ms, its accessibility elements),
// all without input, so it runs in the background:
// - who takes a press, and how long the platform takes to find out;
// - the window resized a step at a time, the page following each step;
// - the accessibility tree a screen reader reads: Pane's controls and the
//   page's own content, in the one window.
const viewsPage: Scenario = async (context) => {
	const app = await launchPane(context);
	const profile = await app.createProfile("Page");
	await app.openPage(profile, context.fixtures.url("light.html?g3"));

	type Hit = { handler: string; target: string; micros: number };

	const hit = async (x: number, y: number) =>
		(await app.send<Hit>("hitTest", { x, y })).result;

	const page = await hit(760, 420);
	const sidebar = await hit(130, 300);

	expect(page.target === "page", `the page's middle: ${JSON.stringify(page)}`);
	expect(sidebar.target === "views", `the sidebar: ${JSON.stringify(sidebar)}`);

	// Steps of 20 points in and back out, as a pointer drags an edge.
	type Step = { ms: number; window: number[]; page: number[] };

	const steps: Step[] = [];

	const widths = [
		...Array.from({ length: 10 }, (_, i) => 1260 - i * 20),
		...Array.from({ length: 10 }, (_, i) => 1100 + i * 20),
	];

	for (const width of widths) {
		const { result } = await app.send<Step>("resizeWindow", {
			width,
			height: 800,
		});

		steps.push(result);
	}

	// The page keeps the panel's inset at the right and bottom each step.
	const off = steps.filter(({ window, page }) => {
		const [, , pageWidth = 0, pageHeight = 0] = page;
		const [x = 0, y = 0] = page;

		return (
			x + pageWidth !== (window[0] ?? 0) - 8 ||
			y + pageHeight !== (window[1] ?? 0) - 8
		);
	});

	expect(off.length === 0, `the page off its panel: ${JSON.stringify(off)}`);

	const times = steps.map((step) => step.ms).sort((a, b) => a - b);

	const timing = {
		hitTestMicros: { page: page.micros, sidebar: sidebar.micros },
		resizeStepMs: {
			median: times[Math.floor(times.length / 2)],
			max: times.at(-1),
			steps: times.length,
		},
	};

	// While the screen is locked, macOS shows an assistive app no window of
	// Pane's at all, only its menus.
	if (await isScreenLocked()) {
		context.defer("the accessibility tree: it needs the screen unlocked");

		return timing;
	}

	// The tree as VoiceOver reads it, Chromium's web part turned on as an
	// assistive app turns it on.
	const { pid } = app.browser;
	await activateWebAccessibility(pid);

	await captureWindow(
		await mainWindowOf(pid),
		join(context.out, "views-page.png"),
	);

	const elements = (await accessibilityTree(pid)).map(describeElement);
	const tree = elements.join("\n");

	const has = (pattern: RegExp) => elements.some((line) => pattern.test(line));

	const reads = {
		heading: has(/AXHeading|A light page/),
		address: has(/AXTextField/),
		rows: has(/AXButton/),
		webArea: has(/AXWebArea/),
	};

	expect(
		Object.values(reads).every(Boolean),
		`the accessibility tree misses some of ${JSON.stringify(reads)}:\n${tree}`,
	);

	return {
		...timing,
		accessibility: { elements: elements.length, ...reads },
	};
};

export default viewsPage;
