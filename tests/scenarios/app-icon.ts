import { bringToFront, launchPane } from "../lib/pane";
import { expect, type Scenario } from "../run";

// The App icon setting (ADR 0089): the Dock shows the sky it names, whatever
// the theme, and automatic shows Day under the light theme and Night under the
// dark one.
const appIcon: Scenario = async (context) => {
	const app = await launchPane(context);

	const cases = [
		{ theme: "light", appIcon: "automatic", dockIcon: "day" },
		{ theme: "dark", appIcon: "automatic", dockIcon: "night" },
		{ theme: "light", appIcon: "night", dockIcon: "night" },
		{ theme: "dark", appIcon: "dusk", dockIcon: "dusk" },
		{ theme: "dark", appIcon: "day", dockIcon: "day" },
		{ theme: "dark", appIcon: "afterglow", dockIcon: "afterglow" },
		{ theme: "light", appIcon: "moonrise", dockIcon: "moonrise" },
		{ theme: "light", appIcon: "automatic", dockIcon: "day" },
	] as const;

	for (const [, step] of cases.entries()) {
		await app.send("theme", { theme: step.theme });
		await app.send("appIcon", { appIcon: step.appIcon });

		const state = await app.waitFor(
			`${step.theme} theme, ${step.appIcon} icon`,
			(state) => state.dockIcon === step.dockIcon,
			5_000,
		);

		expect(
			state.appIcon === step.appIcon && state.dockIcon === step.dockIcon,
			`${step.theme} theme, ${step.appIcon}: the Dock shows ${state.dockIcon}`,
		);
	}

	// Settings' row (the chosen icon) and the picker it opens, in each theme:
	// the window's app UI drawn into a PNG, which a locked screen allows. The
	// row's click opens the picker; a sky clicked in it reaches the Dock with
	// the picker open; Esc or Return closes it: a real key with the front,
	// and without it the same key through Views' own dispatch, where AppKit's
	// path needs the window key.
	await app.send("showPage", { page: "settings" });

	await app.waitFor("Settings", (state) => state.page === "settings", 5_000);

	// Once the picker's fade (Motion.panel, 0.15 s) has settled.
	const render = (file: string) => app.render(file, 400);

	const front =
		context.foreground &&
		(await bringToFront(context, app, "Esc and Return by real keys"));

	for (const [theme, appIcon, close] of [
		["light", "fuchsia", "\u001b"],
		["dark", "moonrise", "\r"],
	] as const) {
		await app.send("theme", { theme });
		await app.send("appIcon", { appIcon: "automatic" });

		await app.waitFor(
			`automatic under the ${theme} theme`,
			(state) => state.dockIcon === (theme === "dark" ? "night" : "day"),
			5_000,
		);

		await render(`app-icon-settings-${theme}.png`);
		await app.click("pane.settings.appIcon");

		await app.waitFor(
			`the picker over ${theme}`,
			(state) => state.appIconPicker,
			5_000,
		);

		await app.click(`pane.settings.appIcon.${appIcon}`);

		const picked = await app.waitFor(
			`${appIcon} from the picker`,
			(state) => state.appIcon === appIcon && state.dockIcon === appIcon,
			5_000,
		);

		expect(
			picked.appIconPicker,
			`the picker closed when ${appIcon} was picked`,
		);

		await render(`app-icon-picker-${theme}.png`);

		await app.send(front ? "key" : "dispatchKey", { key: close });

		await app.waitFor(
			`the picker closed by ${close === "\r" ? "Return" : "Esc"}`,
			(state) => !state.appIconPicker,
			5_000,
		);
	}
};

export default appIcon;
