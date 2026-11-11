import { join } from "node:path";
import type { PaneApp } from "../lib/pane";
import { setUpPin } from "../lib/pane";
import { serveSlowRelease } from "../lib/slow-release";
import { launchCopy, standInDmg } from "../lib/update-copies";
import { expect, type Scenario } from "../run";

// The update's states as the pill, Settings' update row, and the Lock show
// them (DESIGN.md › Updates; Paper 03.3, 03.6, 04.27, 04.28, 06.3, 06.3.1),
// on the run's build, the Views app's or the Swift's, to compare
// (docs/plans/views-migration.md, K1): offered; a download refused, its digest
// another, which offers it again; downloading; ready, staged beside the app
// (Restart to update); and installed by hand, where Pane can't replace itself
// (Quit and install). Whole windows in the run's appearance at the screen's
// scale, Settings scrolled to its end, the Lock's action under the pointer.
// Each copy runs in a folder of its own, since staging writes beside it.

// Where the Lock's update status is in the 1280 × 800 window, for a build
// whose channel names no frames (the Swift's): the Views build's.
const LOCK_STATUS = { x: 640, y: 760.5 };

const PIN = "1234";

const updateStates: Scenario = async (context) => {
	const work = context.tempDir("pane-update-states-");
	// 6 MB, served at 640 KB/s: ten seconds of download to render.
	const dmg = await standInDmg(work, "99.0.0", "99.0.0", 6 * 1024 * 1024);
	let digest = dmg.sha256;
	const server = await serveSlowRelease(dmg, () => digest);
	const releases = server.releases;
	const rendered: string[] = [];
	// Where the Lock's status was hovered, for each render of it.
	const hovered: Record<string, { x: number; y: number }> = {};

	// Where the pill and the update row were, for each render, in points
	// from the window's top left: x, y, width, height (a build whose channel
	// names no frames gives none).
	const regions: Record<string, Record<string, number[]>> = {};

	const render = async (app: PaneApp, name: string, delay = 600) => {
		await app.render(`${name}.png`, delay);
		rendered.push(name);

		regions[name] = await app
			.frames("pane.")
			.then((frames) =>
				Object.fromEntries(
					[
						"pane.sidebar.updatePill",
						"pane.settings.updates",
						"pane.lockScreen.update",
					].flatMap((key) => (frames[key] ? [[key, frames[key]]] : [])),
				),
			)
			.catch(() => ({}));
	};

	try {
		const installs = await launchCopy(context, releases);
		const app = installs.app;

		await app.waitFor(
			"the update offered",
			(state) => state.update.status === "available" && state.update.pill,
			15_000,
		);

		await showUpdateRow(app);
		await render(app, "updates-offered");
		await setUpPin(app, PIN);
		await lockAndRender(app, "lock-offered", render, 600, hovered);

		// Refused: the digest differs, the dmg is deleted, and it is on offer
		// again.
		digest = "0".repeat(64);
		await app.send("checkUpdates");
		await settledCheck(app);
		await app.send("downloadUpdate");

		await app.waitFor(
			"the refused download under way",
			(state) => state.update.status === "downloading",
			10_000,
		);

		await app.waitFor(
			"the download refused",
			(state) => state.update.status === "available",
			30_000,
		);

		await showUpdateRow(app);
		await render(app, "updates-failed");
		digest = dmg.sha256;
		await app.send("checkUpdates");
		await settledCheck(app);

		// Downloading, in Settings and at the Lock.
		await app.send("downloadUpdate");

		await app.waitFor(
			"the download under way",
			(state) =>
				state.update.status === "downloading" &&
				(state.update.progress ?? 0) >= 0.2,
			15_000,
		);

		await render(app, "updates-downloading", 100);
		await lockAndRender(app, "lock-downloading", render, 100, hovered);

		// Ready: staged beside the app.
		const ready = await app.waitFor(
			"the update staged",
			(state) => state.update.status === "downloaded",
			60_000,
		);

		expect(
			ready.update.installsItself,
			`not staged: ${JSON.stringify(ready.update)}`,
		);

		await showUpdateRow(app);
		await render(app, "updates-ready");
		await lockAndRender(app, "lock-ready", render, 600, hovered);
		await app.browser.close();

		// A Pane that can't replace itself: Quit and install.
		const byHand = await launchCopy(context, releases, undefined, {
			PANE_UPDATE_INSTALL: "0",
		});

		await byHand.app.waitFor(
			"the update offered",
			(state) => state.update.status === "available",
			15_000,
		);

		await byHand.app.send("downloadUpdate");

		const downloaded = await byHand.app.waitFor(
			"the download done",
			(state) => state.update.status === "downloaded",
			60_000,
		);

		expect(
			!downloaded.update.installsItself &&
				downloaded.update.label === "Quit and install",
			`by hand: ${JSON.stringify(downloaded.update)}`,
		);

		await showUpdateRow(byHand.app);
		await render(byHand.app, "updates-by-hand");
		await setUpPin(byHand.app, PIN);
		await lockAndRender(byHand.app, "lock-by-hand", render, 600, hovered);
		await byHand.app.browser.close();
	} finally {
		server.stop();
	}

	return { rendered, hovered, regions, out: join(context.out) };
};

// Settings, at its end, where the update row is, the pointer over the page
// and off the pill. Scrolled once it has laid out: the Swift's took no scroll
// before.
async function showUpdateRow(app: PaneApp) {
	await app.send("showPage", { page: "settings" });
	await app.waitFor("Settings", (state) => state.page === "settings");
	await Bun.sleep(500);
	await app.send("scrollWindow", { x: 700, y: 400, dy: -2_000 });
	await app.send("hoverWindow", { x: 860, y: 120 });
}

// A check that has answered, with the update still on offer.
async function settledCheck(app: PaneApp) {
	await app.waitFor(
		"the check answered",
		(state) => state.update.status === "available",
		15_000,
	);

	await Bun.sleep(1_000);
}

// Locked, the Lock's update action under the pointer, rendered, and opened
// again with the PIN.
async function lockAndRender(
	app: PaneApp,
	name: string,
	render: (app: PaneApp, name: string, delay?: number) => Promise<void>,
	delay = 600,
	hovered: Record<string, { x: number; y: number }> = {},
) {
	await app.send("lockNow");
	await app.waitFor("the Lock", (state) => state.security.locked);
	await Bun.sleep(700);

	const frame = await app
		.frames("pane.lockScreen.update")
		.then((frames) => frames["pane.lockScreen.update"])
		.catch(() => undefined);

	const at = frame
		? {
				x: (frame[0] ?? 0) + (frame[2] ?? 0) / 2,
				y: (frame[1] ?? 0) + (frame[3] ?? 0) / 2,
			}
		: LOCK_STATUS;

	// At rest, then with its action under the pointer, which the Swift's
	// channel can't show (its move reaches no SwiftUI hover).
	await app.send("hoverWindow", { x: 860, y: 120 });
	await render(app, name, delay);
	await app.send("hoverWindow", at);
	await render(app, `${name}-hover`, 600);
	hovered[name] = at;
	await app.send("pinKeys", { keys: PIN });
	await app.waitFor("unlocked", (state) => !state.security.locked, 15_000);
}

export default updateStates;
