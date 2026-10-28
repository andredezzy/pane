import { writeFileSync } from "node:fs";
import { join } from "node:path";
import { type AppState, launchPane } from "../lib/pane";
import { expect, type Scenario } from "../run";

type Saved = {
	name: string;
	color: string;
	tabs: { url: string; title: string }[];
}[];

const saved = (state: AppState): Saved =>
	state.profiles.map((profile) => ({
		name: profile.name,
		color: profile.color,
		tabs: profile.tabs.map(({ url, title }) => ({ url, title })),
	}));

// Profiles and their tabs, in order, survive quitting and relaunching.
const restore: Scenario = async (context) => {
	const dataDir = context.tempDir("pane-restore-");
	const first = await launchPane(context, { dataDir });

	const layout: [string, string, string[]][] = [
		["Work", "BLUE", ["Mail", "Docs", "Calendar"]],
		["Personal", "ROSE", ["News", "Music"]],
		["Side project", "EMERALD", ["Repo"]],
	];

	for (const [name, color, tabs] of layout) {
		const profile = await first.createProfile(name, color);

		for (const title of tabs) {
			await first.openTab(
				profile,
				context.fixtures.url(`switcher.html?name=${encodeURIComponent(title)}`),
			);
		}
	}

	// Reorder as a drag would: Calendar to the front of Work, then Side
	// project above Personal.
	const work = (await first.state()).profiles[0]?.id;
	await first.send("moveTab", { profile: work, from: 2, to: 0 });
	await first.send("moveProfile", { from: 2, to: 1 });

	const before = await first.waitFor("every tab titled", (state) =>
		state.profiles
			.flatMap((profile) => profile.tabs)
			.every((tab) => tab.title !== "" && !tab.loading),
	);

	writeFileSync(
		join(context.out, "restore-before.json"),
		`${JSON.stringify(saved(before), null, "\t")}\n`,
	);

	await first.browser.close();

	const second = await launchPane(context, { dataDir });
	const after = await second.state();

	writeFileSync(
		join(context.out, "restore-after.json"),
		`${JSON.stringify(saved(after), null, "\t")}\n`,
	);

	expect(
		saved(before)
			.map((profile) => profile.name)
			.join(",") === "Work,Side project,Personal" &&
			saved(before)[0]
				?.tabs.map((tab) => tab.title)
				.join(",") === "Calendar,Mail,Docs",
		"the reorder did not apply",
	);

	expect(
		JSON.stringify(saved(after)) === JSON.stringify(saved(before)),
		"the relaunched app shows different profiles or tabs",
	);

	expect(
		after.profiles.every(
			(profile) =>
				!profile.expanded && profile.tabs.every((tab) => !tab.loaded),
		),
		"restored profiles should start collapsed, with no page loaded",
	);

	return { dataDir, profiles: saved(after) };
};

export default restore;
