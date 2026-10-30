import { join } from "node:path";
import { canDriveUI, mainWindowOf, windowState } from "../lib/cua";
import { launchPane } from "../lib/pane";
import { expect, type Scenario } from "../run";

// Ctrl+Tab lists recently used tabs grouped by profile; releasing picks one.
const switcher: Scenario = async (context) => {
	const app = await launchPane(context);
	const tabs: Record<string, string> = {};

	for (const [name, color, titles] of [
		["Work", "BLUE", ["Mail", "Docs"]],
		["Personal", "ROSE", ["News", "Music"]],
	] as const) {
		const profile = await app.createProfile(name, color);

		for (const title of titles) {
			tabs[title] = await app.openTab(
				profile,
				context.fixtures.url(`switcher.html?name=${title}`),
			);
		}
	}

	await app.waitFor("every tab titled", (state) =>
		state.profiles
			.flatMap((profile) => profile.tabs)
			.every((tab) => tab.title !== "" && !tab.loading),
	);

	// Use order: Music (current), then Mail, then Docs before it.
	for (const title of ["Docs", "Mail", "Music"]) {
		await app.send("select", { tab: tabs[title] });
	}

	const { state: open } = await app.send("switcher", { action: "next" });

	expect(open.switcher.visible, "the switcher did not open");

	expect(
		open.switcher.groups.map((group) => group.name).join(",") ===
			"Personal,Work",
		`groups: ${open.switcher.groups.map((group) => group.name).join(",")}`,
	);

	const flattened = open.switcher.groups.flatMap((group) => group.tabs);

	expect(
		flattened[open.switcher.selectedIndex] === tabs.Mail,
		"selection should start at the previous tab",
	);

	await Bun.sleep(300);
	await app.send("render", { path: join(context.out, "switcher.png") });

	let screenshot: string | null = null;

	if (!(await canDriveUI())) {
		context.defer("the switcher as the screen shows it");
	} else {
		const windowId = await mainWindowOf(app.browser.pid);

		await windowState(
			app.browser.pid,
			windowId,
			join(context.out, "switcher-screen.png"),
		);

		screenshot = "switcher-screen.png";
	}

	await app.send("switcher", { action: "next" });
	const { state: committed } = await app.send("switcher", { action: "commit" });

	expect(!committed.switcher.visible, "the switcher stayed open");

	expect(
		committed.activeTabID === tabs.Docs,
		`commit selected ${Object.entries(tabs).find(([, id]) => id === committed.activeTabID)?.[0] ?? committed.activeTabID}, not Docs`,
	);

	return {
		groups: open.switcher.groups,
		render: "switcher.png",
		screenshot,
		selected: committed.activeTabID,
	};
};

export default switcher;
