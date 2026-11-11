import { join } from "node:path";
import { stats } from "../lib/benchmark";
import { extensionId, workerPage } from "../lib/extensions";
import type { AppState, PaneApp } from "../lib/pane";
import { type FrameInterval, signpostFrameIntervals } from "../lib/signposts";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// Extensions, X1 and X2 (docs/plans/views-migration.md, Phase 3), on the
// Views build, in the background:
// - the toolbar's pinned action and the puzzle, and the extensions menu
//   (Paper 04.6) empty and with the probe, and its more menu (04.9), drawn
//   with the pointer off them, under the names the Swift's renders took
//   (menu-empty, menu, more-menu); the pinned action with a badge, dimmed
//   while its action doesn't show, and lit in its capsule while its popup
//   shows;
// - the popup's bubble (Paper 04.12), from its own window;
// - what VoiceOver reads of them (ADR 0071), and the pinned action's
//   right-click menu as its model holds it (ADR 0096);
// - timed by the app's signposts: the puzzle's press to the frame showing the
//   menu ("Open extensions menu"), and a pinned action's press to its popup's
//   first frame ("Show extension popup").
//
//   bun tests/run.ts --binary ~/chromium/src/out/Release/Pane.app/Contents/MacOS/Pane \
//     --out <dir>/light --appearance light tests/rigs/views-extensions.ts

const ROUNDS = 20;

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

// The pointer off every control, over the page's place, so a render shows
// no hover; a moment for the hover's fade.
async function park(app: PaneApp) {
	await app.send("hoverWindow", { x: 700, y: 600 });
	await Bun.sleep(300);
}

const viewsExtensions: Scenario = async (context) => {
	const { app } = await launchTraced(context, "views-extensions", {
		args: [`--load-extension=${PROBE}`],
	});

	const profile = await app.createProfile("Extensions", "BLUE");
	const url = context.fixtures.url("light.html?views-extensions");
	await app.openPage(profile, url);
	const id = await extensionId(app, profile, "Pane action probe");
	const worker = await workerPage(app, id, url);
	const result: Record<string, unknown> = {};

	const item = (state: AppState) =>
		(state.extensions[profile] ?? []).find((each) => each.id === id);

	const openMenu = async () => {
		await app.send("extensionsMenu", { control: "puzzle" });
		await app.waitFor("the menu", (state) => state.extensionsMenu);
	};

	const closeMenu = async () => {
		await app.send("dispatchKey", { key: "\u001b" });
		await app.waitFor("the menu closed", (state) => !state.extensionsMenu);
	};

	// Pinned, while its worker runs: a badge the extension set, no color of
	// its own, the Swift's red under white; and its action off on the page,
	// the button at 40%.
	await app.send("setExtensionPinned", { id, profile, pinned: true });

	await app.waitFor(
		"the probe pinned",
		(state) => item(state)?.pinned === true,
	);

	await worker.evaluate(`chrome.action.setBadgeText({ text: "3" })`);
	await Bun.sleep(500);
	await park(app);
	await app.render("toolbar-badge.png");
	await worker.evaluate(`chrome.action.disable()`);
	await Bun.sleep(500);
	await app.render("toolbar-dimmed.png");
	await worker.evaluate(`chrome.action.enable()`);
	await worker.evaluate(`chrome.action.setBadgeText({ text: "" })`);
	await Bun.sleep(300);

	// The menu with nothing on.
	await app.send("setExtensionEnabled", { id, profile, enabled: false });
	await app.waitFor("the probe off", (state) => item(state)?.enabled === false);
	await openMenu();
	await park(app);
	await app.render("menu-empty.png");
	result.emptyFrames = await app.frames("pane.extensionsMenu");
	await closeMenu();

	// The probe on and pinned.
	await app.send("setExtensionEnabled", { id, profile, enabled: true });
	await app.send("setExtensionPinned", { id, profile, pinned: true });

	await app.waitFor(
		"the probe pinned",
		(state) => item(state)?.pinned === true && item(state)?.enabled === true,
		30_000,
	);

	await openMenu();
	await park(app);
	await app.render("menu.png");
	result.menuFrames = await app.frames("pane.extensionsMenu");
	result.menuAccessibility = (await app.send("extensionsAccessibility")).result;

	await app.send("extensionsMenu", { control: "more", id });
	await app.waitFor("the more menu", (state) => state.extensionMoreMenu === id);
	await park(app);
	await app.render("more-menu.png");
	result.moreFrames = await app.frames("pane.extensionMoreMenu");
	result.moreAccessibility = (await app.send("extensionsAccessibility")).result;

	// Esc: the more menu, then the menu.
	await app.send("dispatchKey", { key: "\u001b" });

	await app.waitFor(
		"the more menu closed, the menu still open",
		(state) => state.extensionMoreMenu === null && state.extensionsMenu,
	);

	await closeMenu();

	// The pinned action's right-click menu, as a right-click makes it.
	result.actionMenu = (await app.send("extensionActionMenu", { id })).result;

	// Its popup, from its button: the bubble, the button lit in its capsule.
	await app.click(`pane.extension.${id}`);

	const popup = await app.waitFor(
		"the popup",
		(state) => state.extensionPopup?.open === true,
		30_000,
	);

	result.popup = popup.extensionPopup;
	await app.renderPopup("popup.png", 500);
	await park(app);
	await app.render("toolbar-popup.png");

	result.popupAccessibility = (
		await app.send("extensionsAccessibility")
	).result;

	result.popupWindows = popup.nativeWindows;

	await app.send("closeExtensionPopup");
	await app.waitFor("the popup gone", (state) => state.extensionPopup === null);

	const intervals = await signpostFrameIntervals(async () => {
		for (let index = 0; index < ROUNDS; index++) {
			await app.send("extensionsMenu", { control: "puzzle" });
			await app.waitFor("the menu", (state) => state.extensionsMenu);
			await Bun.sleep(150);
			await app.send("dispatchKey", { key: "\u001b" });
			await app.waitFor("the menu closed", (state) => !state.extensionsMenu);
			await Bun.sleep(150);
		}

		for (let index = 0; index < ROUNDS / 2; index++) {
			await app.click(`pane.extension.${id}`);

			await app.waitFor(
				"the popup",
				(state) => state.extensionPopup?.open === true,
				30_000,
			);

			await Bun.sleep(300);
			await app.send("closeExtensionPopup");

			await app.waitFor(
				"the popup gone",
				(state) => state.extensionPopup === null,
			);

			await Bun.sleep(300);
		}

		return app.browser.pid;
	});

	const timings: Record<string, unknown> = {};

	for (const name of ["Open extensions menu", "Show extension popup"]) {
		const each = intervals[name] ?? [];
		expect(each.length > 0, `${name}: no intervals`);
		timings[name] = timed(each);
	}

	result.timings = timings;

	return result;
};

export default viewsExtensions;
