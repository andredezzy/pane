import { framesDuring, statesByProcess } from "../lib/frames";
import { launchPane, trafficLightsInPlace } from "../lib/pane";
import { expect, type Scenario } from "../run";

// The Views prototype's run (docs/plans/views-migration.md, Phase 1): two
// profiles, a page each, a switch between them, the sheet over the page, a
// new tab and the address field from the menu, renders. For the Views
// build, from out/Release:
//
//   bun tests/run.ts --binary ~/chromium/src/out/Release/Pane.app/Contents/MacOS/Pane \
//     --out <dir> tests/rigs/views-prototype.ts
const viewsPrototype: Scenario = async (context) => {
	const app = await launchPane(context);
	const work = await app.createProfile("Work");
	const home = await app.createProfile("Home");
	const a = await app.openPage(work, context.fixtures.url("light.html?work"));
	const b = await app.openPage(home, context.fixtures.url("dark.html?home"));
	const log: unknown[] = [];
	let { state } = await app.send("state");

	expect(
		trafficLightsInPlace(state.window.trafficLightFrames),
		`the traffic lights at ${JSON.stringify(state.window.trafficLightFrames)}`,
	);

	log.push({
		afterOpen: state.activeTabID === b.tab,
		profiles: state.profiles.length,
	});

	// The switch to the other profile morphs its mark on the spring (G5):
	// the window's own frames, the browser process's, all presented in full.
	const reports = await framesDuring(app.browser.cdp, async () => {
		({ state } = await app.send("select", { tab: a.tab }));
		await Bun.sleep(600);
	});

	const morph = statesByProcess(reports)[app.browser.pid] ?? {};

	expect(
		(morph.STATE_PRESENTED_ALL ?? 0) > 10 &&
			!morph.STATE_DROPPED &&
			!morph.STATE_PRESENTED_PARTIAL,
		`the morph's frames: ${JSON.stringify(morph)}`,
	);

	log.push({
		selectedA: state.activeTabID === a.tab,
		activeProfile: state.activeProfileID === work,
		morph,
	});

	await app.render("window-a.png");
	({ state } = await app.send("select", { tab: b.tab }));

	log.push({
		selectedB: state.activeTabID === b.tab,
		activeProfile: state.activeProfileID === home,
	});

	await app.render("window-b.png");
	await app.send("openProfileSheet");
	await Bun.sleep(400);
	await app.render("sheet.png");
	await app.send("closeProfileSheet");

	// ⌘T answers before the engine makes its page (the Swift app's
	// AfterFrame): the tab, selected, and the address field are in the
	// state at once; a second ⌘T right behind gives the first its page
	// behind the second; each page keeps the id its row showed before it.
	const tabsInHome = () =>
		state.profiles.find((p) => p.id === home)?.tabs ?? [];

	({ state } = await app.send("menu", { path: ["File", "New Tab"] }));
	const first = state.activeTabID;

	expect(
		tabsInHome().length === 2 &&
			tabsInHome().at(-1)?.id === first &&
			state.focus === "address",
		`⌘T's tab and address field at once: ${JSON.stringify(state)}`,
	);

	({ state } = await app.send("menu", { path: ["File", "New Tab"] }));
	const second = state.activeTabID;
	await Bun.sleep(500);
	({ state } = await app.send("state"));

	expect(
		JSON.stringify(tabsInHome().map((tab) => tab.id)) ===
			JSON.stringify([b.tab, first, second]) &&
			state.activeTabID === second &&
			second !== first &&
			state.focus === "address",
		`two ⌘T in a row, each page under its row's id: ${JSON.stringify(state)}`,
	);

	log.push({ tabsInHome: tabsInHome().length });

	await app.send("menu", { path: ["Navigate", "Focus Address Bar"] });
	await app.quit();

	return log;
};

export default viewsPrototype;
