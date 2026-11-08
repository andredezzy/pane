import { launchPane } from "../lib/pane";
import type { Scenario } from "../run";

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
	log.push({
		afterOpen: state.activeTabID === b.tab,
		profiles: state.profiles.length,
	});
	({ state } = await app.send("select", { tab: a.tab }));
	log.push({
		selectedA: state.activeTabID === a.tab,
		activeProfile: state.activeProfileID === work,
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
	await app.send("menu", { path: ["File", "New Tab"] });
	await Bun.sleep(500);
	({ state } = await app.send("state"));
	log.push({
		tabsInHome: state.profiles.find((p) => p.id === home)?.tabs.length,
	});
	await app.send("menu", { path: ["Navigate", "Focus Address Bar"] });
	await app.quit();

	return log;
};

export default viewsPrototype;
