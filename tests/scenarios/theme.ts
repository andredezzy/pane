import { launchPane } from "../lib/pane";
import { pollValue } from "../lib/poll";
import { expect, type Scenario } from "../run";

// The theme setting drives the app's appearance and the pages' color scheme.
const theme: Scenario = async (context) => {
	const app = await launchPane(context);
	const profile = await app.createProfile("Theme");
	const url = context.fixtures.url("theme.html");
	const { page } = await app.openPage(profile, url);

	const systemIsDark =
		(
			await Bun.$`defaults read -g AppleInterfaceStyle`.quiet().nothrow().text()
		).trim() === "Dark";

	for (const value of ["light", "dark", "system"] as const) {
		const expectedDark =
			value === "dark" || (value === "system" && systemIsDark);

		await app.send("theme", { theme: value });

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
			appearance ===
				(expectedDark ? "NSAppearanceNameDarkAqua" : "NSAppearanceNameAqua"),
			`theme ${value}: app appearance ${appearance}`,
		);
	}
};

export default theme;
