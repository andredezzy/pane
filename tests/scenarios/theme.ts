import { launchPane } from "../lib/pane";
import { pollValue } from "../lib/poll";
import { expect, type Scenario } from "../run";

// The theme setting drives the app's appearance and the pages' color scheme.
// Chosen as the user chooses it: in Settings' Theme menu, through its model,
// as a click in the system's menu chooses (the menu itself, on screen, is the
// front's). The app's appearance is what its window draws with, and the menus
// it opens take: light or dark.
const theme: Scenario = async (context) => {
	const app = await launchPane(context);
	const profile = await app.createProfile("Theme");
	const url = context.fixtures.url("theme.html");
	const { page } = await app.openPage(profile, url);

	// A run's --appearance stands in for the Mac's, which System follows.
	const systemIsDark = context.appearance
		? context.appearance === "dark"
		: (
				await Bun.$`defaults read -g AppleInterfaceStyle`
					.quiet()
					.nothrow()
					.text()
			).trim() === "Dark";

	await app.send("showPage", { page: "settings" });
	await app.waitFor("Settings", (state) => state.page === "settings");

	for (const [value, title] of [
		["light", "Light"],
		["dark", "Dark"],
		["system", "System"],
	] as const) {
		const expectedDark =
			value === "dark" || (value === "system" && systemIsDark);

		await app.send("settingsMenu", {
			menu: "pane.settings.theme",
			choose: title,
		});

		await app.waitFor(`the ${value} theme`, (state) => state.theme === value);

		const pageDark = await pollValue(
			() =>
				page.evaluate<boolean>(
					"matchMedia('(prefers-color-scheme: dark)').matches",
				),
			(dark) => dark === expectedDark,
			10_000,
			200,
		);

		expect(
			pageDark === expectedDark,
			`theme ${value}: page dark = ${pageDark}`,
		);

		const { appearance } = await app.state();

		expect(
			appearance === (expectedDark ? "dark" : "light"),
			`theme ${value}: app appearance ${appearance}`,
		);
	}
};

export default theme;
