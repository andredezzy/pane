import { $ } from "bun";
import { launchPane } from "../lib/pane";
import { expect, type Scenario } from "../run";

// The theme setting drives the app's appearance and the pages' color scheme.
const theme: Scenario = async (context) => {
	const app = await launchPane(context);
	const profile = await app.createProfile("Theme");
	const url = context.fixtures.url("theme.html");
	const tab = await app.openTab(profile, url);
	const page = await app.page(tab, url);

	const systemIsDark =
		(
			await $`defaults read -g AppleInterfaceStyle`.quiet().nothrow().text()
		).trim() === "Dark";

	const results: Record<
		string,
		{ appearance: string; pageDark: boolean; expectedDark: boolean }
	> = {};

	for (const value of ["light", "dark", "system"] as const) {
		const expectedDark =
			value === "dark" || (value === "system" && systemIsDark);

		await app.send("theme", { theme: value });

		const deadline = Date.now() + 10_000;
		let pageDark = !expectedDark;

		while (pageDark !== expectedDark && Date.now() < deadline) {
			pageDark = await page.evaluate<boolean>(
				"matchMedia('(prefers-color-scheme: dark)').matches",
			);

			if (pageDark !== expectedDark) {
				await Bun.sleep(200);
			}
		}

		const { appearance } = await app.state();
		results[value] = { appearance, pageDark, expectedDark };

		expect(
			pageDark === expectedDark,
			`theme ${value}: page dark = ${pageDark}`,
		);

		expect(
			appearance ===
				(expectedDark ? "NSAppearanceNameDarkAqua" : "NSAppearanceNameAqua"),
			`theme ${value}: app appearance ${appearance}`,
		);
	}

	return { systemIsDark, results };
};

export default theme;
