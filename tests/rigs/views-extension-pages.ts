import { writeFileSync } from "node:fs";
import { join } from "node:path";
import { stats } from "../lib/benchmark";
import type { Page } from "../lib/cdp";
import {
	DETAILS,
	detailsURL,
	EXTENSIONS_PAGE,
	extensionsPage,
	ITEMS,
	press,
	SHORTCUTS,
	SHORTCUTS_PAGE,
	SIDEBAR,
} from "../lib/chrome-extensions";
import {
	DARK_READER,
	extensionId,
	pressToolbarAction,
} from "../lib/extensions";
import type { AppState, PaneApp } from "../lib/pane";
import { poll } from "../lib/poll";
import { type FrameInterval, signpostFrameIntervals } from "../lib/signposts";
import { waitForSheet } from "../lib/surfaces";
import { captureWindow, swiftTool } from "../lib/tools";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// Extensions, X3 to X5 (docs/plans/views-migration.md, Phase 3), on the
// Views build, in the background:
// - the prompts (ADR 0056): adding, Dia's dialog over the scrim (Paper
//   04.10), from the Web Store's own install of Dark Reader, and removing,
//   Aside's popup under the probe's button (04.11), each rendered light and
//   dark at 2× under the Swift's names (install-popup, remove-popup), the
//   pointer off them, and what assistive technologies read of them;
// - Chrome's extensions pages in the profile's tab (D3, Paper 10.1 to 10.3):
//   the list, the probe's Details, and the shortcuts page, the window as the
//   channel renders it with the page's own capture in its place, as Paper's
//   boards were made, light and dark;
// - an action's popup above its button, with the window placed low on the
//   screen, where the room above it is more than below (X2's carried
//   finding): its edge, and its bubble rendered;
// - timed by the app's signposts: Chrome's hook asking to add, and Remove
//   from Pane's press, each to its prompt's first frame ("Extension
//   prompt"), and Manage all extensions to the frame
//   showing its tab ("Open extensions page") and on to the page's first
//   paint ("Extensions page painted").
//
//   bun tests/run.ts --binary ~/chromium/src/out/Release/Pane.app/Contents/MacOS/Pane \
//     --out <dir>/light --appearance light tests/rigs/views-extension-pages.ts

const ROUNDS = 12;

const PROBE = join(
	import.meta.dir,
	"..",
	"pages",
	"extensions",
	"action-probe",
);

const round = (value: number) => Math.round(value * 10) / 10;

function summary(values: number[]) {
	const sorted = [...values].sort((a, b) => a - b);
	const { median } = stats(sorted);

	return {
		medianMs: round(median),
		p10Ms: round(sorted[Math.floor(sorted.length * 0.1)] ?? median),
		p90Ms: round(sorted[Math.floor(sorted.length * 0.9)] ?? median),
		runs: values.length,
	};
}

function timed(intervals: FrameInterval[]) {
	return {
		toScreen: summary(intervals.map((value) => value.ms)),
		toCommit: summary(
			intervals.flatMap((value) =>
				value.committedMs === undefined ? [] : [value.committedMs],
			),
		),
	};
}

// The pointer over the page's place, off every control, so a render shows
// no hover; a moment for the hover's fade.
async function park(app: PaneApp) {
	await app.send("hoverWindow", { x: 700, y: 650 });
	await Bun.sleep(300);
}

// The test's own window, as the screen shows it, at the display's scale:
// only the main window of the app this run launched, by its number.
async function capture(state: AppState, file: string) {
	const main = state.nativeWindows.find((window) => window.role === "main");

	expect(
		main?.number,
		`no number for the main window: ${JSON.stringify(state.nativeWindows)}`,
	);

	expect(
		await captureWindow(main.number, file),
		`the main window ${main.number} was not captured`,
	);
}

// Pane's window as the channel renders it at 2×, with the page's own capture
// (CDP) in the page's place, as Paper's 10.1 to 10.3 were made: a capture of
// the window in the background can show what it last drew while hidden.
async function composite(app: PaneApp, page: Page, file: string) {
	const stem = file.replace(/\.png$/, "");
	await app.send("render", { path: `${stem}-window.png` });

	const [x = 0, y = 0] =
		(await app.frames("pane.content"))["pane.content"] ?? [];

	const { data } = await page.send<{ data: string }>("Page.captureScreenshot", {
		format: "png",
	});

	writeFileSync(`${stem}-page.png`, Buffer.from(data, "base64"));
	const paste = await swiftTool("image-paste");
	await Bun.$`${paste} ${stem}-window.png ${stem}-page.png ${x * 2} ${y * 2} ${file}`.quiet();
}

const viewsExtensionPages: Scenario = async (context) => {
	const { app } = await launchTraced(context, "views-extension-pages", {
		args: [`--load-extension=${PROBE}`],
	});

	const profile = await app.createProfile("Extensions", "BLUE");
	const url = context.fixtures.url("light.html?views-extension-pages");
	const { tab } = await app.openPage(profile, url);
	const id = await extensionId(app, profile, "Pane action probe");
	const result: Record<string, unknown> = {};

	const theme = async (name: "light" | "dark") => {
		await app.send("theme", { theme: name });

		await app.waitFor(
			`the ${name} appearance`,
			(state) => state.appearance === name,
		);
	};

	// Removing: the probe pinned, its more menu's Remove from Pane.
	await app.send("setExtensionPinned", { id, profile, pinned: true });

	await app.waitFor("the probe pinned", (state) =>
		(state.extensions[profile] ?? []).some(
			(each) => each.id === id && each.pinned,
		),
	);

	const askRemove = async () => {
		await app.send("extensionsMenu", { control: "puzzle" });
		await app.waitFor("the menu", (state) => state.extensionsMenu);
		await app.send("extensionsMenu", { control: "more", id });

		await app.waitFor(
			"the more menu",
			(state) => state.extensionMoreMenu === id,
		);

		await app.send("extensionsMenu", { control: "remove" });

		return waitForSheet(app, "extensionRemove");
	};

	const cancel = async () => {
		await app.send("dispatchKey", { key: "\u001b" });
		await app.waitFor("the prompt gone", (state) => state.sheet === null);
	};

	for (const name of ["light", "dark"] as const) {
		await theme(name);
		await askRemove();
		await park(app);
		await app.render(`remove-popup-${name}.png`);
		result[`removeFrames-${name}`] = await app.frames("pane.extensionPopup");
		await cancel();
	}

	result.removeAccessibility = await app.state().then((state) => state.sheet);

	// Adding: Dark Reader, as the Web Store's own install asks.
	await theme("light");
	await app.send("installExtension", { id: DARK_READER, profile });
	const adding = await waitForSheet(app, "extensionInstall", 60_000);
	result.install = adding;
	await park(app);
	await app.render("install-popup-light.png");
	result.installFrames = await app.frames("pane.extensionInstall");
	await theme("dark");

	// Pane's icon in the dialog follows the Dock's, which the Theme turned
	// to Night as the dialog asked.
	await app.waitFor(
		"the Dock's Night icon",
		(state) => state.dockIcon === "night",
	);

	await park(app);
	await app.render("install-popup-dark.png");

	await app.send("clickSheetButton", {
		title: adding.buttons?.[1] ?? "Cancel",
	});

	await app.waitFor("the dialog gone", (state) => state.sheet === null);

	// Chrome's pages in the profile's tab, light and dark.
	for (const name of ["light", "dark"] as const) {
		await theme(name);
		await app.send("select", { tab });
		await app.send("extensionsMenu", { control: "puzzle" });
		await app.waitFor("the menu", (state) => state.extensionsMenu);
		await app.send("extensionsMenu", { control: "manage" });
		const { page } = await extensionsPage(app, profile, EXTENSIONS_PAGE);
		await park(app);
		await Bun.sleep(700);

		await composite(app, page, join(context.out, `page-list-${name}.png`));

		await app.send("extensionActionMenu", { id, run: "Manage Extension" });
		const details = await extensionsPage(app, profile, detailsURL(id));
		await park(app);
		await Bun.sleep(700);

		await composite(
			app,
			details.page,
			join(context.out, `page-details-${name}.png`),
		);

		await press(
			page,
			`${SIDEBAR}.querySelector("#sectionsShortcuts")`,
			"Keyboard shortcuts",
		);

		await poll(
			"the shortcuts page",
			() =>
				page
					.evaluate<boolean>(
						`location.href === ${JSON.stringify(SHORTCUTS_PAGE)} && !!${SHORTCUTS}?.querySelector("#container")`,
					)
					.catch(() => false),
			10_000,
		);

		await park(app);
		await Bun.sleep(700);

		await composite(app, page, join(context.out, `page-shortcuts-${name}.png`));
	}

	await theme("light");

	// Details' Site settings: Chrome's settings page for the extension, in a
	// new tab of the profile (Paper 10.2's note asked whether Pane serves
	// chrome://settings).
	await app.send("extensionActionMenu", { id, run: "Manage Extension" });
	const settingsFrom = await extensionsPage(app, profile, detailsURL(id));

	await press(
		settingsFrom.page,
		`${DETAILS}.querySelector("#siteSettings")`,
		"Site settings",
	);

	const settings = await app
		.waitFor(
			"chrome://settings in a tab of the profile",
			(state) =>
				(state.profiles.find((each) => each.id === profile)?.tabs ?? []).some(
					(each) =>
						each.id === state.activeTabID &&
						each.url.startsWith("chrome://settings") &&
						!each.loading,
				),
			15_000,
		)
		.catch((error: Error) => error.message);

	result.siteSettings =
		typeof settings === "string"
			? settings
			: settings.profiles
					.find((each) => each.id === profile)
					?.tabs.find((each) => each.id === settings.activeTabID);

	if (typeof settings !== "string") {
		await Bun.sleep(700);
		await capture(settings, join(context.out, "page-site-settings.png"));
		await app.send("closeTab", { tab: settings.activeTabID });
	}

	// The popup above its button, with the window low on the screen.
	await app.send("select", { tab });

	const placed = await app.send<{ screen: number[]; window: number[] }>(
		"resizeWindow",
		{
			width: 1280,
			height: 800,
			x: 120,
			y: "low",
		},
	);

	result.placed = placed.result;
	await pressToolbarAction(app, profile, id);

	const above = await app.waitFor(
		"the popup shown",
		(state) => state.extensionPopup?.open === true,
		30_000,
	);

	result.popupAbove = above.extensionPopup;

	expect(
		above.extensionPopup?.above === true,
		`the popup opened ${JSON.stringify(above.extensionPopup)} with the window low`,
	);

	await app.renderPopup("popup-above.png", 500);
	result.popupWindows = above.nativeWindows;
	await app.send("closeExtensionPopup");
	await app.waitFor("the popup gone", (state) => state.extensionPopup === null);
	await app.send("resizeWindow", { width: 1280, height: 800, x: 120, y: 80 });

	// The timings. Adding first, on its own: Chrome's hook asking to the
	// dialog's first frame, each round the Web Store's own install of Dark
	// Reader, declined with Esc. In the light run alone: each round asks the
	// store, and the dark run is for the renders.
	const timesAdding = context.appearance !== "dark";

	const installIntervals = timesAdding
		? await signpostFrameIntervals(async () => {
				for (let index = 0; index < ROUNDS / 2; index++) {
					await app.send("installExtension", { id: DARK_READER, profile });
					await waitForSheet(app, "extensionInstall", 60_000);
					await Bun.sleep(150);
					await cancel();
					await Bun.sleep(150);
				}

				return app.browser.pid;
			})
		: {};

	const intervals = await signpostFrameIntervals(async () => {
		for (let index = 0; index < ROUNDS; index++) {
			await askRemove();
			await Bun.sleep(150);
			await cancel();
			await Bun.sleep(150);
		}

		for (let index = 0; index < ROUNDS / 2; index++) {
			await app.send("select", { tab });
			await app.send("extensionsMenu", { control: "puzzle" });
			await app.waitFor("the menu", (state) => state.extensionsMenu);
			await app.send("extensionsMenu", { control: "manage" });

			const { tab: pageTab } = await extensionsPage(
				app,
				profile,
				EXTENSIONS_PAGE,
			);

			await Bun.sleep(400);
			await app.send("closeTab", { tab: pageTab });

			await app.waitFor(
				"the page's tab gone",
				(state) =>
					!state.profiles.some((each) =>
						each.tabs.some((tabState) => tabState.id === pageTab),
					),
			);

			await Bun.sleep(300);
		}

		return app.browser.pid;
	});

	const timings: Record<string, unknown> = {};

	for (const [label, each] of [
		["Extension prompt, adding", installIntervals["Extension prompt"]],
		["Extension prompt, removing", intervals["Extension prompt"]],
		["Open extensions page", intervals["Open extensions page"]],
		["Extensions page painted", intervals["Extensions page painted"]],
	] as const) {
		if (label.endsWith("adding") && !timesAdding) {
			continue;
		}

		expect(each && each.length > 0, `${label}: no intervals`);
		timings[label] = timed(each ?? []);
	}

	result.timings = timings;

	// Chrome's page's own Remove (chrome.management.uninstall, its
	// confirmation): what shows over the window, kept as found (ADR 0062).
	await app.send("select", { tab });
	await app.send("extensionsMenu", { control: "puzzle" });
	await app.waitFor("the menu", (state) => state.extensionsMenu);
	await app.send("extensionsMenu", { control: "manage" });
	const removal = await extensionsPage(app, profile, EXTENSIONS_PAGE);

	await press(
		removal.page,
		`${ITEMS}.querySelector("extensions-item#${id}").shadowRoot.querySelector("#removeButton")`,
		"the card's Remove",
	);

	await Bun.sleep(1_500);
	const asking = await app.state();
	result.chromeRemove = { windows: asking.nativeWindows, sheet: asking.sheet };
	await capture(asking, join(context.out, "page-remove.png"));

	for (const [index, window] of asking.nativeWindows.entries()) {
		if (window.role !== "main" && window.number) {
			await captureWindow(
				window.number,
				join(context.out, `page-remove-window-${index}.png`),
			);
		}
	}

	return result;
};

export default viewsExtensionPages;
