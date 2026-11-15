import { bringToFront, launchPane } from "../lib/pane";
import { bySystem } from "../lib/platform";
import { expect, type Scenario } from "../run";

// About Pane (Paper 11.15 A). On Windows, Pane's own dialog over the window,
// as Exit Pane? is: the App icon's sky as the taskbar shows it, "Pane", its
// version, and the copyright line both systems show, which a screen reader
// hears as a dialog named "About Pane"; OK closes it, and Enter and Esc do
// where the run lets Pane take the keys. The item is no longer greyed out.
// On the Mac it is AppKit's standard panel, which the run leaves alone: its
// item shows, the system enabling it.
const PANES_OWN_ABOUT = bySystem({ mac: false, windows: true });

const COPYRIGHT =
	"Copyright 2026 Rounded Corners. Built on Chromium, copyright The Chromium Authors.";

type Item = { title: string; enabled: boolean; submenu?: Item[] };

type Read = Record<string, { role: string; name: string; modal: boolean }>;

const about: Scenario = async (context) => {
	const app = await launchPane(context);

	const bar = (await app.send<Item[]>("menuBar")).result;

	const item = bar
		.find((menu) => menu.title === "Pane")
		?.submenu?.find((each) => each.title === "About Pane");

	expect(!!item, `no About Pane in ${JSON.stringify(bar.map((m) => m.title))}`);

	if (!PANES_OWN_ABOUT) {
		return;
	}

	expect(item?.enabled === true, "About Pane is greyed out");

	const open = async () => {
		await app.send("menu", { path: ["Pane", "About Pane"] });

		return (await app.waitFor("About Pane", (state) => !!state.about)).about;
	};

	const shown = await open();

	expect(
		shown?.lines[0] === "Pane" &&
			/^Version \d+\.\d+\.\d+$/.test(shown.lines[1] ?? "") &&
			shown.lines[2] === COPYRIGHT &&
			shown.icon,
		`About Pane shows ${JSON.stringify(shown)}`,
	);

	const read = (await app.send<Read>("accessibility", { prefix: "pane.about" }))
		.result;

	expect(
		read["pane.about"]?.role === "dialog" &&
			read["pane.about"].name === "About Pane" &&
			read["pane.about"].modal &&
			read["pane.about.ok"]?.name === "OK",
		`About Pane as a screen reader hears it: ${JSON.stringify(read)}`,
	);

	await app.render("about.png", 300);

	// OK closes it.
	const frames = (
		await app.send<Record<string, number[]>>("frames", {
			prefix: "pane.about",
		})
	).result;

	const ok = frames["pane.about.ok"];

	expect(!!ok, `no OK in ${JSON.stringify(frames)}`);

	if (ok) {
		const [x = 0, y = 0, width = 0, height = 0] = ok;
		await app.send("clickWindow", { x: x + width / 2, y: y + height / 2 });
		await app.waitFor("About Pane closed by OK", (state) => !state.about);
	}

	// Enter and Esc close it too, where the window takes keys.
	if (!(await bringToFront(context, app, "About Pane's keys"))) {
		return;
	}

	for (const [name, key] of [
		["Enter", "\r"],
		["Esc", "\u001b"],
	] as const) {
		await open();
		await app.send("key", { key });
		await app.waitFor(`About Pane closed by ${name}`, (state) => !state.about);
	}
};

export default about;
