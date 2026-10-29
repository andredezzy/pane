import { join } from "node:path";
import { launchPane } from "../lib/pane";
import { expect, type Scenario } from "../run";

// The App icon setting (ADR 0089): the Dock shows the sky it names, whatever
// the theme, and automatic shows Day under the light theme and Night under the
// dark one.
const appIcon: Scenario = async (context) => {
	const app = await launchPane(context);
	const results: Record<string, { appIcon: string; dockIcon: string }> = {};

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

	for (const [index, step] of cases.entries()) {
		await app.send("theme", { theme: step.theme });
		await app.send("appIcon", { appIcon: step.appIcon });

		const state = await app.waitFor(
			`${step.theme} theme, ${step.appIcon} icon`,
			(state) => state.dockIcon === step.dockIcon,
			5_000,
		);

		results[`${index + 1}. ${step.theme}, ${step.appIcon}`] = {
			appIcon: state.appIcon,
			dockIcon: state.dockIcon,
		};

		expect(
			state.appIcon === step.appIcon && state.dockIcon === step.dockIcon,
			`${step.theme} theme, ${step.appIcon}: the Dock shows ${state.dockIcon}`,
		);
	}

	// Settings' tiles, the chosen one ringed, in each theme: the window's app
	// UI drawn into a PNG, which a locked screen allows.
	await app.send("showPage", { page: "settings" });
	const renders: string[] = [];

	for (const [theme, appIcon] of [
		["light", "automatic"],
		["dark", "moonrise"],
	] as const) {
		await app.send("theme", { theme });
		await app.send("appIcon", { appIcon });
		const file = `app-icon-settings-${theme}.png`;
		await app.send("render", { path: join(context.out, file) });
		renders.push(file);
	}

	return { results, renders };
};

export default appIcon;
