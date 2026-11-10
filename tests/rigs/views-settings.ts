import { readFileSync } from "node:fs";
import * as net from "node:net";
import { join } from "node:path";
import { stats } from "../lib/benchmark";
import { launchPane, type PaneApp, setUpPin, tabOf } from "../lib/pane";
import { signpostIntervals } from "../lib/signposts";
import { expect, type Scenario } from "../run";

// H6, the panels (docs/plans/views-migration.md, Phase 3), on the Views
// build, in the background. Settings' renders, for the Swift's (surfaces'
// settings-top, settings-end, settings-updates-checking,
// app-icon-picker-automatic and -dusk, settings-app-icon-dusk,
// settings-pin-set, updates-available, -downloading, and -downloaded) and for
// what they miss: the picker on every sky, each menu's trigger under the
// pointer, and each menu's items as its model holds them (the system's menu
// itself shows on screen, which only a session with the front may open).
// settings.json as the menus left it, and the update row before any check.
// Then how long Settings, a Theme, the picker, and a sky take from their
// input to the frame on screen, read from the app's signposts. For the Views
// build, from out/Release:
//
//   bun tests/run.ts --binary ~/chromium/src/out/Release/Pane.app/Contents/MacOS/Pane \
//     --out <dir> [--appearance light|dark] tests/rigs/views-settings.ts

const MENUS = [
	"pane.settings.theme",
	"pane.settings.tabSleep",
	"pane.settings.profileUnload",
	"pane.settings.cacheBudget",
] as const;

const SKIES = [
	"automatic",
	"day",
	"clouds",
	"morning",
	"dusk",
	"fuchsia",
	"sunset",
	"twilight",
	"afterglow",
	"night",
	"moonrise",
] as const;

type MenuItem = {
	title: string;
	enabled: boolean;
	checked: boolean;
	icon: boolean;
};

// A tab at `url`, once loaded and titled.
async function loadTab(
	app: PaneApp,
	profile: string,
	url: string,
): Promise<string> {
	const tab = await app.openTab(profile, url);

	await app.waitFor(
		`${url} loaded`,
		(state) => {
			const shown = tabOf(state, tab);

			return shown?.url === url && !shown.loading && shown.title !== "";
		},
		30_000,
	);

	return tab;
}

// A frame's middle, from the window's top left.
async function middleOf(app: PaneApp, name: string) {
	const [x = 0, y = 0, width = 0, height = 0] =
		(await app.frames(name))[name] ?? [];

	expect(width > 0 && height > 0, `${name} does not show`);

	return { x: x + width / 2, y: y + height / 2 };
}

// The pointer off every control: over the sidebar's list below its rows.
const away = (app: PaneApp) => app.send("hoverWindow", { x: 130, y: 600 });

// A press and its release at a control's middle, through Views' own
// dispatch, which reaches the window in the background (pointerWindow).
async function press(app: PaneApp, name: string) {
	const at = await middleOf(app, name);
	await app.send("pointerWindow", { phase: "down", ...at });
	await app.send("pointerWindow", { phase: "up", ...at });
}

async function scrollToEnd(app: PaneApp) {
	await app.send("scrollWindow", { x: 700, y: 400, dy: -2_000 });
}

async function scrollToTop(app: PaneApp) {
	await app.send("scrollWindow", { x: 700, y: 400, dy: 2_000 });
}

async function menu(
	app: PaneApp,
	name: string,
	choose?: string,
): Promise<MenuItem[]> {
	return (
		await app.send<MenuItem[]>("settingsMenu", {
			menu: name,
			...(choose && { choose }),
		})
	).result;
}

// Settings at a profile with one tab, as surfaces' second copy: from the
// top and at its end; a check running; each menu's items, and a choice in
// each; each trigger under the pointer; the picker on every sky; and the
// PIN set.
async function settings(
	context: Parameters<Scenario>[0],
	log: Record<string, unknown>,
) {
	const app = await launchPane(context);

	try {
		const work = await app.createProfile("Work", "BLUE");
		const url = context.fixtures.url("nav-a.html?settings");
		const tab = await loadTab(app, work, url);

		// The page counts its frames: under Settings it draws none (ADR 0073).
		const page = await app.page(tab, url);

		await page.evaluate(
			"window.frames_ = 0; (function tick() { window.frames_++; requestAnimationFrame(tick); })(); 0",
		);

		// The sidebar's Settings, pressed.
		await press(app, "pane.sidebar.settings");

		const shown = await app.waitFor(
			"Settings, after the launch's check",
			(state) =>
				state.page === "settings" &&
				state.update.checked &&
				state.update.status === "idle",
			15_000,
		);

		expect(
			shown.focus === "pane.settings",
			`Settings showed with ${shown.focus} focused`,
		);

		const framesBefore = await page.evaluate<number>("window.frames_");
		await Bun.sleep(2_000);

		log.pageFramesUnderSettings =
			(await page.evaluate<number>("window.frames_")) - framesBefore;

		expect(
			log.pageFramesUnderSettings === 0,
			`the page drew ${log.pageFramesUnderSettings} frames in 2 s under Settings`,
		);

		await away(app);
		await app.render("surfaces-settings-top.png", 600);
		await scrollToEnd(app);
		await app.render("surfaces-settings-end.png", 600);

		await app.send("checkUpdates");

		await app.waitFor(
			"the check running",
			(state) => state.update.status === "checking",
			5_000,
		);

		await app.render("surfaces-settings-updates-checking.png", 100);

		await app.waitFor(
			"the check done",
			(state) => state.update.status === "idle",
			15_000,
		);

		// The App icon's press opens the picker over Settings at its end, as
		// the Swift's rig had it; a sky pressed in it reaches the Dock with the
		// picker open, which Esc closes.
		await press(app, "pane.settings.appIcon");
		await app.waitFor("the picker", (state) => state.appIconPicker, 5_000);
		await away(app);
		await app.render("surfaces-app-icon-picker-automatic.png", 600);

		// Automatic shows Day while the app is light, Night while it's dark.
		const shownFor = (sky: string, appearance: string) => {
			if (sky !== "automatic") {
				return sky;
			}

			return appearance === "dark" ? "night" : "day";
		};

		for (const sky of SKIES) {
			await press(app, `pane.settings.appIcon.${sky}`);

			const picked = await app.waitFor(
				`${sky} from the picker`,
				(state) =>
					state.appIcon === sky &&
					state.dockIcon === shownFor(sky, state.appearance),
				5_000,
			);

			expect(picked.appIconPicker, `the picker closed when ${sky} was picked`);
			await away(app);

			await app.render(
				sky === "dusk"
					? "surfaces-app-icon-picker-dusk.png"
					: `app-icon-picker-${sky}.png`,
				400,
			);
		}

		await press(app, "pane.settings.appIcon.dusk");
		await app.waitFor("Dusk", (state) => state.dockIcon === "dusk", 5_000);
		await app.send("dispatchKey", { key: "\u001b" });

		await app.waitFor(
			"the picker closed by Esc",
			(state) => !state.appIconPicker,
			5_000,
		);

		await app.render("surfaces-settings-app-icon-dusk.png", 600);

		// Return closes it too, and Done.
		await press(app, "pane.settings.appIcon");
		await app.waitFor("the picker", (state) => state.appIconPicker, 5_000);
		await app.send("dispatchKey", { key: "\r" });

		await app.waitFor(
			"the picker closed by Return",
			(state) => !state.appIconPicker,
			5_000,
		);

		await press(app, "pane.settings.appIcon");
		await app.waitFor("the picker", (state) => state.appIconPicker, 5_000);
		await press(app, "pane.appIcon.done");

		await app.waitFor(
			"the picker closed by Done",
			(state) => !state.appIconPicker,
			5_000,
		);

		// A click outside it, on the scrim.
		await press(app, "pane.settings.appIcon");
		await app.waitFor("the picker", (state) => state.appIconPicker, 5_000);
		await app.send("pointerWindow", { phase: "down", x: 40, y: 400 });
		await app.send("pointerWindow", { phase: "up", x: 40, y: 400 });

		await app.waitFor(
			"the picker closed by a click outside it",
			(state) => !state.appIconPicker,
			5_000,
		);

		await app.send("appIcon", { appIcon: "automatic" });
		await scrollToTop(app);

		// Each menu as its model holds it, the setting checked; a choice in
		// it reaches the settings.
		const menus: Record<string, MenuItem[]> = {};

		for (const name of MENUS) {
			menus[name] = await menu(app, name);
		}

		log.menus = menus;

		expect(
			JSON.stringify(
				menus["pane.settings.theme"]?.map((item) => item.title),
			) === JSON.stringify(["System", "Light", "Dark"]) &&
				menus["pane.settings.theme"]?.[0]?.checked === true,
			`Theme's menu: ${JSON.stringify(menus["pane.settings.theme"])}`,
		);

		for (const [name, choice] of [
			["pane.settings.tabSleep", "1 hour"],
			["pane.settings.profileUnload", "Off"],
			["pane.settings.cacheBudget", "1 GB"],
		] as const) {
			await menu(app, name, choice);
			const items = await menu(app, name);
			const checked = items.find((item) => item.checked)?.title ?? "";
			expect(checked === choice, `${name}: ${checked} after ${choice}`);
		}

		await app.render("settings-menus-chosen.png", 400);

		// Saved as the model saves a change: settings.json, plain, as it's read
		// before the Lock.
		await app.send("save");

		// PersistedFile's envelope: { state: { settings }, version }.
		const saved = JSON.parse(
			readFileSync(join(app.dataDir, "settings.json"), "utf8"),
		).state.settings;

		log.saved = saved;

		expect(
			saved.tabSleepAfterMinutes === 60 &&
				saved.profileUnloadAfterMinutes === null &&
				saved.cacheBudgetMB === 1024,
			`settings.json after the menus: ${JSON.stringify(saved)}`,
		);

		// Back to the defaults, for the renders that follow.
		await menu(app, "pane.settings.tabSleep", "15 min");
		await menu(app, "pane.settings.profileUnload", "30 min");
		await menu(app, "pane.settings.cacheBudget", "300 MB");

		// Each trigger under the pointer: the foreground at 4% (ADR 0063),
		// which it keeps while its menu shows.
		for (const name of MENUS) {
			await app.send("hoverWindow", await middleOf(app, name));
			await app.render(`settings-${name.split(".").pop()}-hover.png`, 600);
		}

		await app.send("hoverWindow", await middleOf(app, "pane.settings.appIcon"));
		await app.render("settings-appIcon-hover.png", 600);
		await away(app);

		// The PIN set: Change and Remove, and Lock after.
		await setUpPin(app, "1234");

		await app.waitFor(
			"Lock after shown",
			(state) => state.page === "settings" && state.security.pinLength === 4,
			5_000,
		);

		const withPin = await app.frames("pane.settings.");

		expect(
			"pane.settings.changePIN" in withPin &&
				"pane.settings.removePIN" in withPin &&
				"pane.settings.lockAfter" in withPin &&
				!("pane.settings.setUpPIN" in withPin),
			`the PIN's rows: ${Object.keys(withPin).join(", ")}`,
		);

		log.lockAfter = await menu(app, "pane.settings.lockAfter");
		await scrollToEnd(app);
		await app.render("surfaces-settings-pin-set.png", 800);
	} finally {
		await app.browser.close();
	}
}

// Offered 99.0.0 with no profile, as surfaces' third copy: Settings' Updates
// with the update on offer; then, with a PIN set and Pane locked and opened
// again, its download under way, and done (Quit and install: the fixtures'
// dmg holds no Pane to stage).
async function updates(
	context: Parameters<Scenario>[0],
	log: Record<string, unknown>,
) {
	const app = await launchPane(context, {
		env: {
			PANE_UPDATE_URL: context.fixtures.url("releases/latest?tag=v99.0.0"),
			PANE_UPDATE_DOWNLOADS: context.tempDir("pane-settings-updates-"),
			PANE_UPDATE_OPEN: "0",
		},
	});

	try {
		await app.waitFor(
			"the update offered",
			(state) => state.update.status === "available" && state.update.pill,
			15_000,
		);

		await app.send("showPage", { page: "settings" });
		await app.waitFor("Settings", (state) => state.page === "settings");
		await scrollToEnd(app);
		await app.render("surfaces-updates-available.png", 600);

		// A PIN set, Pane locked and opened again, as surfaces' third copy did
		// before the download (its Lock render is L1's).
		await setUpPin(app, "1234");
		await app.send("lockNow");
		await app.waitFor("the Lock", (state) => state.security.locked);
		await app.send("pinKeys", { keys: "1234" });
		await app.waitFor("unlocked", (state) => !state.security.locked, 15_000);
		await app.send("showPage", { page: "settings" });
		await app.waitFor("Settings", (state) => state.page === "settings");
		await scrollToEnd(app);

		// Settings' Download update, pressed.
		await press(app, "pane.settings.downloadUpdate");

		// 4 MB in 16 chunks, 250 ms apart.
		await app.waitFor(
			"the download under way",
			(state) =>
				state.update.status === "downloading" &&
				(state.update.progress ?? 0) >= 0.25,
			15_000,
		);

		await app.render("surfaces-updates-downloading.png", 0);

		const done = await app.waitFor(
			"the download done",
			(state) => state.update.status === "downloaded",
			30_000,
		);

		await app.render("surfaces-updates-downloaded.png", 600);
		log.downloaded = done.update;
	} finally {
		await app.browser.close();
	}
}

// A server that takes a connection and never answers: a check that runs on.
async function silentServer(): Promise<{ port: number; stop: () => void }> {
	const sockets: net.Socket[] = [];

	const server = net.createServer((socket) => {
		socket.on("error", () => socket.destroy());
		sockets.push(socket);
	});

	const port = await new Promise<number>((listening) => {
		server.listen(0, "127.0.0.1", () => {
			listening((server.address() as net.AddressInfo).port);
		});
	});

	return {
		port,
		stop: () => {
			for (const socket of sockets) {
				socket.destroy();
			}

			server.close();
		},
	};
}

// Never checked (Paper 06.3's first rows): the release's server never answers,
// so the launch's check runs on: what the row says before any check, and
// Checking…, disabled.
async function neverChecked(
	context: Parameters<Scenario>[0],
	log: Record<string, unknown>,
) {
	const silent = await silentServer();

	const app = await launchPane(context, {
		env: {
			PANE_UPDATE_URL: `http://127.0.0.1:${silent.port}/releases/latest`,
		},
	});

	try {
		await app.send("showPage", { page: "settings" });

		const checking = await app.waitFor(
			"Settings, the launch's check running",
			(state) =>
				state.page === "settings" && state.update.status === "checking",
			10_000,
		);

		const frames = await app.frames("pane.settings.");

		log.neverChecked = { update: checking.update, rows: Object.keys(frames) };

		expect(
			!checking.update.checked &&
				"pane.settings.updates" in frames &&
				"pane.settings.checkUpdates" in frames,
			`never checked: ${JSON.stringify(log.neverChecked)}`,
		);

		await scrollToEnd(app);
		await app.render("settings-updates-never-checked.png", 600);
	} finally {
		silent.stop();
		await app.browser.close();
	}
}

// From each input to the frame on screen (the app's signposts, Instruments'
// Points of Interest): Settings shown by the sidebar's press, a Theme chosen
// in its menu, the picker opened by the App icon's press, and a sky chosen
// in it.
async function timing(
	context: Parameters<Scenario>[0],
	log: Record<string, unknown>,
) {
	const rounds = 10;

	const intervals = await signpostIntervals(async () => {
		const app = await launchPane(context);

		try {
			const work = await app.createProfile("Timing", "BLUE");
			await loadTab(app, work, context.fixtures.url("nav-a.html?timing"));
			await Bun.sleep(1_000);

			for (let round = 0; round < rounds; round++) {
				await press(app, "pane.sidebar.settings");
				await app.waitFor("Settings", (state) => state.page === "settings");
				await Bun.sleep(250);
				await press(app, "pane.sidebar.settings");
				await app.waitFor("the page", (state) => state.page === "browser");
				await Bun.sleep(250);
			}

			await press(app, "pane.sidebar.settings");
			await app.waitFor("Settings", (state) => state.page === "settings");

			for (let round = 0; round < rounds; round++) {
				for (const theme of ["Dark", "Light"]) {
					await menu(app, "pane.settings.theme", theme);

					await app.waitFor(
						theme,
						(state) => state.theme === theme.toLowerCase(),
					);

					await Bun.sleep(300);
				}
			}

			await menu(app, "pane.settings.theme", "System");

			for (let round = 0; round < rounds; round++) {
				await press(app, "pane.settings.appIcon");
				await app.waitFor("the picker", (state) => state.appIconPicker);
				await Bun.sleep(300);

				await press(
					app,
					`pane.settings.appIcon.${round % 2 ? "dusk" : "night"}`,
				);

				await Bun.sleep(300);
				await app.send("dispatchKey", { key: "\u001b" });
				await app.waitFor("the picker closed", (state) => !state.appIconPicker);
				await Bun.sleep(300);
			}

			await app.send("appIcon", { appIcon: "automatic" });
			await Bun.sleep(1_000);
		} finally {
			await app.browser.close();
		}

		return undefined;
	});

	log.timing = Object.fromEntries(
		Object.entries(intervals).map(([name, values]) => [name, stats(values)]),
	);
}

const viewsSettings: Scenario = async (context) => {
	const log: Record<string, unknown> = {};

	await settings(context, log);
	await updates(context, log);
	await neverChecked(context, log);
	await timing(context, log);

	return log;
};

export default viewsSettings;
