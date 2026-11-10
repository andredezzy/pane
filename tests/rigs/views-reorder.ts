import { writeFileSync } from "node:fs";
import { join } from "node:path";
import { type AppState, launchPane, type PaneApp, tabOf } from "../lib/pane";
import { swiftTool } from "../lib/tools";
import { expect, type Scenario } from "../run";

// H3, reorder (docs/plans/views-migration.md, Phase 3), on the Views build,
// in the background. Its renders: a tab dragged down its profile as the
// Swift's surfaces rig dragged Docs (surfaces-sidebar-reorder-dragging), the
// rows mid-slide, a profile dragged as Paper 03.11 draws one, and the sidebar
// after each drop beside the same order reached with no drag, which must
// match it to the pixel: a drop leaves nothing behind. And a drag step's
// time, from its event to the window's layout, over 120 moves through the
// profiles and through a profile's tabs, as ADR 0066 timed the Swift's.
//
//   bun tests/run.ts --binary ~/chromium/src/out/Release/Pane.app/Contents/MacOS/Pane \
//     --out <dir> [--appearance light|dark] tests/rigs/views-reorder.ts

type Step = { ms: number };

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

// The median, the 90th percentile, and the largest of `times`.
function spread(times: number[]) {
	const sorted = [...times].sort((a, b) => a - b);

	const at = (share: number) =>
		sorted[Math.min(sorted.length - 1, Math.floor(share * sorted.length))] ??
		Number.NaN;

	return {
		count: sorted.length,
		median: at(0.5),
		p90: at(0.9),
		max: sorted.at(-1) ?? Number.NaN,
	};
}

const viewsReorder: Scenario = async (context) => {
	const { fixtures } = context;
	const dataDir = context.tempDir("pane-reorder-");
	const log: Record<string, unknown> = {};
	let app = await launchPane(context, { dataDir });

	// The surfaces rig's sidebar: five profiles, Work's three tabs, Proxied's
	// one, Personal's two.
	const work = await app.createProfile("Work", "BLUE");
	const proxied = await app.createProfile("Proxied", "EMERALD");
	const refused = await app.createProfile("Wrong proxy password", "ROSE");
	const silent = await app.createProfile("Silent proxy", "AMBER");
	const personal = await app.createProfile("Personal", "VIOLET");

	await loadTab(app, work, fixtures.url("switcher.html?name=Calendar"));

	const docs = await loadTab(
		app,
		work,
		fixtures.url("switcher.html?name=Docs"),
	);

	const home = await loadTab(app, work, fixtures.url("nav-a.html?work"));

	const through = await loadTab(
		app,
		proxied,
		fixtures.url("switcher.html?name=Through%20the%20proxy"),
	);

	for (const name of ["Notes", "Music"]) {
		await loadTab(app, personal, fixtures.url(`switcher.html?name=${name}`));
	}

	await app.waitFor(
		"Navigation A's favicon saved",
		(state) => tabOf(state, home)?.hasFavicon === true,
		10_000,
	);

	// Relaunched, Calendar has no page and dims, as the Swift's slept.
	await app.send("save");
	await app.browser.close();
	app = await launchPane(context, { dataDir });

	for (const tab of [docs, through, home]) {
		await app.send("select", { tab });

		await app.waitFor(
			"the tab's page",
			(state) => tabOf(state, tab)?.loaded === true,
			15_000,
		);
	}

	for (const [profile, status] of [
		[proxied, "connected"],
		[refused, "failed"],
		[silent, "failed"],
	] as const) {
		await app.send("setProxyStatus", { profile, status });
	}

	// The pointer away from the rows: no hover in the renders.
	const away = () => app.send("hoverWindow", { x: 130, y: 600 });
	await away();

	const tabsOf = (state: AppState) =>
		state.profiles
			.find((each) => each.id === work)
			?.tabs.map((each) => each.id) ?? [];

	const profilesOf = (state: AppState) => state.profiles.map((each) => each.id);

	const before = tabsOf(await app.state());

	expect(
		before.length === 3 && before[1] === docs && before[2] === home,
		`Work's tabs: ${before}`,
	);

	// Moves paced as a hand's, each step's time to the window's layout kept.
	const moves = async (
		x: number,
		points: number[],
		pause: number,
		into: number[],
	) => {
		for (const y of points) {
			into.push(
				(await app.send<Step>("pointerWindow", { phase: "drag", x, y })).result
					.ms,
			);

			if (pause > 0) {
				await Bun.sleep(pause);
			}
		}
	};

	// Docs, from its row's middle, as the surfaces rig dragged it: 44 points
	// down in five moves, a frame or two apart. The copy is held at the last
	// tab's place, and Navigation A has slid up.
	const y = 43 + 36 + 4 + before.indexOf(docs) * 36 + 16;
	const path = [4, 12, 24, 36, 44].map((dy) => y + dy);
	const surfacesSteps: number[] = [];
	await app.render("reorder-tab-before.png", 500);
	await app.send("pointerWindow", { phase: "down", x: 100, y });
	await moves(100, path, 40, surfacesSteps);
	await app.render("reorder-tab-dragging.png", 500);
	log.tabDragging = await app.frames();
	await moves(100, [...path].reverse().concat(y), 40, surfacesSteps);
	await app.send("pointerWindow", { phase: "up", x: 100, y });
	await Bun.sleep(500);
	await away();
	await app.render("reorder-tab-back.png", 500);

	expect(
		tabsOf(await app.state()).join() === before.join(),
		`the drag back moved the tabs: ${before} → ${tabsOf(await app.state())}`,
	);

	// Mid-slide: 70 ms after the move that sends Docs past Navigation A.
	await app.send("pointerWindow", { phase: "down", x: 100, y });
	await moves(100, [y + 4, y + 12], 40, surfacesSteps);
	await moves(100, [y + 24], 0, surfacesSteps);
	await app.render("reorder-tab-sliding.png", 70);
	log.tabSliding = await app.frames();
	// Dropped last, then the same order with no drag beside it.
	await moves(100, [y + 40, y + 60], 40, surfacesSteps);
	await app.send("pointerWindow", { phase: "up", x: 100, y: y + 60 });

	await app.waitFor(
		"Docs dropped last",
		(state) => tabsOf(state).at(-1) === docs,
		3_000,
	);

	await Bun.sleep(400);
	await away();
	await app.render("reorder-tab-dropped.png", 500);
	log.tabDropped = await app.frames();
	await app.send("moveTab", { profile: work, from: 2, to: 1 });
	await app.send("moveTab", { profile: work, from: 1, to: 2 });
	await app.render("reorder-tab-moved.png", 500);
	await app.send("moveTab", { profile: work, from: 2, to: 1 });

	// The profiles, collapsed: Work dragged 70 down, past Proxied and onto
	// the wrong password's place, as Paper 03.11 drags Alpha.
	for (const profile of [work, proxied]) {
		await app.send("unloadProfile", { profile });
	}

	await app.waitFor("every profile collapsed", (state) =>
		state.profiles.every((each) => !each.expanded),
	);

	const profiles = profilesOf(await app.state());
	const header = 43 + 18;
	await app.render("reorder-profile-before.png", 600);
	await app.send("pointerWindow", { phase: "down", x: 100, y: header });

	await moves(
		100,
		[6, 20, 40, 55, 70].map((dy) => header + dy),
		40,
		surfacesSteps,
	);

	await app.render("reorder-profile-dragging.png", 500);
	log.profileDragging = await app.frames();
	await app.send("pointerWindow", { phase: "up", x: 100, y: header + 70 });

	await app.waitFor(
		"Work dropped third",
		(state) => profilesOf(state)[2] === work,
		3_000,
	);

	await Bun.sleep(400);
	await away();
	await app.render("reorder-profile-dropped.png", 500);
	await app.send("moveProfile", { from: 2, to: 0 });
	await app.send("moveProfile", { from: 0, to: 2 });
	await app.render("reorder-profile-moved.png", 500);
	await app.send("moveProfile", { from: 2, to: 0 });

	await app.waitFor(
		"the profiles back in order",
		(state) => profilesOf(state).join() === profiles.join(),
	);

	// A drag step's time, over 120 moves 16 ms apart, with Work and Proxied
	// open (11 rows): Proxied's header up to the list's top, down past its
	// end, and back; then Docs among Work's tabs.
	for (const tab of [home, through]) {
		await app.send("select", { tab });
	}

	await app.waitFor(
		"Work and Proxied open",
		(state) =>
			state.profiles.find((each) => each.id === work)?.expanded === true &&
			state.profiles.find((each) => each.id === proxied)?.expanded === true,
	);

	await Bun.sleep(500);
	const frames = await app.frames("pane.");

	const middle = (name: string) => {
		const [, top = 0, , height = 0] = frames[name] ?? [];

		return top + height / 2;
	};

	const wave = (from: number, low: number, high: number, count: number) => {
		const points: number[] = [];
		const legs = [from, low, high, from];
		const perLeg = count / 3;

		for (let leg = 0; leg < 3; leg++) {
			const start = legs[leg] ?? from;
			const end = legs[leg + 1] ?? from;

			for (let step = 1; step <= perLeg; step++) {
				points.push(Math.round(start + ((end - start) * step) / perLeg));
			}
		}

		return points;
	};

	const timed = async (from: number, low: number, high: number) => {
		const points = wave(from, low, high, 120);
		const steps: number[] = [];
		await app.send("pointerWindow", { phase: "down", x: 100, y: from });
		await moves(100, points, 16, steps);
		await app.send("pointerWindow", { phase: "up", x: 100, y: from });
		await Bun.sleep(600);
		// The step that starts the drag, 5 points out: the rows' layers and
		// the copy.
		const start = points.findIndex((y) => Math.abs(y - from) >= 5);

		return {
			start: steps[start] ?? Number.NaN,
			rest: spread(steps.filter((_, index) => index !== start)),
			steps,
		};
	};

	const proxiedHeader = middle(`pane.profile.${proxied}`);
	const docsRow = middle(`pane.tab.${docs}`);
	const profileTimes = await timed(proxiedHeader, 30, 460);
	const tabTimes = await timed(docsRow, 60, 200);
	log.timing = { profiles: profileTimes, tabs: tabTimes };
	log.surfacesSteps = spread(surfacesSteps);

	// An open profile dragged: its whole section, header, tabs, and New tab,
	// stands at 40% where it would drop, below the copy of its header.
	const workHeader = middle(`pane.profile.${work}`);
	await app.send("pointerWindow", { phase: "down", x: 100, y: workHeader });

	await moves(
		100,
		[6, 30, 60, 90, 120].map((dy) => workHeader + dy),
		40,
		surfacesSteps,
	);

	await app.render("reorder-profile-open-dragging.png", 500);
	log.profileOpenDragging = await app.frames();

	await app.send("pointerWindow", {
		phase: "up",
		x: 100,
		y: workHeader + 120,
	});

	await app.waitFor(
		"Work dropped second",
		(state) => profilesOf(state)[1] === work,
		3_000,
	);

	await app.send("moveProfile", { from: 1, to: 0 });

	expect(
		profilesOf(await app.state()).join() === profiles.join() &&
			tabsOf(await app.state()).join() === before.join(),
		"the timed drags, ending where they began, moved a row",
	);

	// A drop leaves nothing behind: each render after one is, to the pixel,
	// the same order shown with no drag.
	const imageDiff = await swiftTool("image-diff");

	for (const [after, without] of [
		["reorder-tab-back.png", "reorder-tab-before.png"],
		["reorder-tab-dropped.png", "reorder-tab-moved.png"],
		["reorder-profile-dropped.png", "reorder-profile-moved.png"],
	] as const) {
		const differs = (
			await Bun.$`${imageDiff} ${join(context.out, after)} ${join(context.out, without)}`.text()
		).trim();

		log[`${after} against ${without}`] = differs;

		expect(
			differs.startsWith("0.0000%"),
			`${after} differs from ${without}: ${differs}`,
		);
	}

	writeFileSync(
		join(context.out, "views-reorder-log.json"),
		`${JSON.stringify(log, null, "\t")}\n`,
	);
};

export default viewsReorder;
