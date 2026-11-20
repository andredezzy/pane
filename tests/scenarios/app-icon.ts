import { existsSync } from "node:fs";
import { bringToFront, launchPane, type PaneApp } from "../lib/pane";
import { bySystem } from "../lib/platform";
import { pollValue } from "../lib/poll";
import {
	holdMapped,
	inTheTestVM,
	setSystemDark,
	shortcutIcon,
	startShortcut,
	systemIsDark,
} from "../lib/tools";
import { expect, type Scenario, type ScenarioContext } from "../run";

// The App icon setting (ADR 0089): the Dock shows the sky it names, whatever
// the theme, and automatic shows Day under the light theme and Night under the
// dark one. On Windows, with Pane's theme on System, Automatic follows
// Windows' own dark and light, three rounds of each, and Start's shortcut
// follows with it, eight more: in the test VM alone, whose Windows no one
// uses; the Mac's appearance is its user's, which no scenario changes.
const followsTheSystem = bySystem({
	mac: async (_context: ScenarioContext, _app: PaneApp): Promise<void> => {},
	windows: async (context: ScenarioContext, app: PaneApp): Promise<void> => {
		if (!(await inTheTestVM())) {
			context.defer(
				"Automatic following Windows' dark and light: the test VM alone changes Windows' theme",
			);

			return;
		}

		// The run's appearance stands in for Windows' own, which then
		// reaches Pane no more.
		if (context.appearance) {
			context.defer(
				"Automatic following Windows' dark and light: --appearance stands in for Windows' own",
			);

			return;
		}

		const dark = await systemIsDark();
		await app.send("theme", { theme: "system" });
		await app.send("appIcon", { appIcon: "automatic" });

		try {
			for (let round = 1; round <= 3; round++) {
				for (const [windowsDark, sky] of [
					[true, "night"],
					[false, "day"],
				] as const) {
					await setSystemDark(windowsDark);

					await app.waitFor(
						`Windows ${windowsDark ? "dark" : "light"}, round ${round}: ${sky}`,
						(state) => state.dockIcon === sky,
						15_000,
					);
				}
			}

			// Start's shortcut, which the copy's first launch made in its own
			// home as the install makes the user's, names the sky of Windows'
			// last change, read right after each of eight changes each way;
			// every second light change finds it held mapped 1.5 s, as Windows'
			// shell maps it while it reads it for Pane's taskbar button. A
			// rewrite refused meanwhile (ERROR_USER_MAPPED_FILE) is tried again,
			// where it once stayed on Night until Windows' next change, the
			// taskbar's button with it (ADR 0089).
			const link = startShortcut(app.home);
			expect(existsSync(link), `no Start shortcut in the copy's home: ${link}`);

			for (let round = 1; round <= 8; round++) {
				for (const [windowsDark, sky] of [
					[true, "night"],
					[false, "day"],
				] as const) {
					const release =
						!windowsDark && round % 2 === 0
							? await holdMapped(link)
							: undefined;

					await setSystemDark(windowsDark);

					await app.waitFor(
						`Windows ${windowsDark ? "dark" : "light"}, shortcut round ${round}: ${sky}`,
						(state) => state.dockIcon === sky,
						15_000,
					);

					if (release) {
						await Bun.sleep(1_500);
						await release();
					}

					const icon = await pollValue(
						() => shortcutIcon(link),
						(named) => named.toLowerCase() === `appicon-${sky}.ico`,
						15_000,
						250,
					);

					expect(
						icon.toLowerCase() === `appicon-${sky}.ico`,
						`Windows ${windowsDark ? "dark" : "light"}, shortcut round ${round}${release ? ", held mapped 1.5 s" : ""}: Start's shortcut names ${icon}, not ${sky}'s`,
					);
				}
			}
		} finally {
			await setSystemDark(dark);
		}
	},
});

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

	await followsTheSystem(context, app);

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
