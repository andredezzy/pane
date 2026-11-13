import { launchPane, type PaneApp, tabOf } from "../lib/pane";
import { silentServer } from "../lib/proxies";
import { expect, type Scenario } from "../run";

// H4, the toolbar (docs/plans/views-migration.md, Phase 3), on the Views
// build, in the background. Its renders, for the Swift's (surfaces'
// toolbar-loading-start, toolbar-loading-slow, toolbar-address-focused,
// window-two-profiles' row, update-pill-available, updates-downloading and
// -downloaded; updates' update-pill) and for Paper 03.3 and 03.4's states the
// renders miss: Back taking presses, under the pointer and pressed; the
// address under the pointer; a new tab's address; the pill under the
// pointer, over its X, and downloading under the pointer. For the Views
// build, from out/Release:
//
//   bun tests/run.ts --binary ~/chromium/src/out/Release/Pane.app/Contents/MacOS/Pane \
//     --out <dir> [--appearance light|dark] tests/rigs/views-toolbar.ts

// The pointer off every control: over the sidebar's list below its rows,
// which Views draws (through the channel, a move over the page reaches the
// page's view, not Views, and one at the window's edge its frame).
const away = (app: PaneApp) => app.send("hoverWindow", { x: 130, y: 600 });

async function toolbar(
	context: Parameters<Scenario>[0],
	log: Record<string, unknown>,
) {
	const { fixtures } = context;
	const silent = await silentServer();
	const app = await launchPane(context);

	try {
		const work = await app.createProfile("Work", "BLUE");
		const home = await app.loadTab(work, fixtures.url("nav-a.html?work"));

		// At rest: the domain, the divider, and the title; Back and Forward
		// with nowhere to go, at 40% (Paper 03.4's second row).
		await away(app);
		await app.render("toolbar-rest.png", 600);

		const rest = await app.state();

		log.rest = rest.toolbar;

		expect(
			rest.toolbar?.addressLabel === true &&
				rest.toolbar.controls["pane.toolbar.back"]?.enabled === false &&
				rest.toolbar.controls["pane.toolbar.forward"]?.enabled === false,
			`at rest: ${JSON.stringify(rest.toolbar)}`,
		);

		// The address under the pointer: the foreground at 3%.
		await app.send("hoverWindow", await app.middleOf("pane.toolbar.address"));
		await app.render("toolbar-address-hover.png", 600);
		await away(app);

		// Focused, its URL selected (surfaces' toolbar-address-focused), as ⌘L
		// leaves it: the Focus Address Bar item's. The window isn't the key
		// one, so the selection is the system's unemphasized gray, as the
		// Swift's was in its background render.
		await app.send("menu", { path: ["Navigate", "Focus Address Bar"] });

		await app.waitFor(
			"the address focused",
			(state) => state.focus === "pane.toolbar.address",
			5_000,
		);

		await app.render("toolbar-address-focused.png", 300);
		// The page takes the keys back.
		await app.send("select", { tab: home });

		// Somewhere to go back to: Back takes presses; under the pointer, the
		// accent fills it; pressed, the press over it.
		await app.send("navigate", { input: fixtures.url("nav-b.html?work") });

		await app.waitFor(
			"Back taking presses",
			(state) =>
				state.toolbar?.controls["pane.toolbar.back"]?.enabled === true &&
				tabOf(state, home)?.loading === false,
		);

		await app.render("toolbar-back.png", 600);
		const back = await app.middleOf("pane.toolbar.back");
		await app.send("hoverWindow", back);
		await app.render("toolbar-back-hover.png", 600);
		await app.send("pointerWindow", { phase: "down", ...back });
		await app.render("toolbar-back-pressed.png", 100);
		// Let go away from it: no click.
		await app.send("pointerWindow", { phase: "up", x: 130, y: 600 });
		await away(app);

		// A load the server holds: the bar at its start, fading in, and Stop
		// in Reload's place (surfaces' toolbar-loading-start), then five
		// seconds on, the slow mode's shimmer (toolbar-loading-slow).
		const inbox = await app.loadTab(
			work,
			fixtures.url("switcher.html?name=Inbox"),
		);

		await app.send("navigate", {
			input: `http://127.0.0.1:${silent.port}/inbox`,
		});

		await app.waitFor(
			"the bar shown",
			(state) =>
				tabOf(state, inbox)?.loading === true &&
				state.progressBar.started &&
				state.progressBar.alpha === 1,
			5_000,
		);

		await app.render("toolbar-loading-start.png", 150);

		await app.waitFor(
			"the slow mode",
			(state) => state.progressBar.shimmer,
			10_000,
		);

		await app.render("toolbar-loading-slow.png", 150);
		const loading = await app.state();

		log.loading = {
			progressBar: loading.progressBar,
			reload: loading.toolbar?.controls["pane.toolbar.reload"]?.name,
		};

		expect(
			loading.toolbar?.controls["pane.toolbar.reload"]?.name === "Stop",
			"Reload did not read Stop while the page loaded",
		);

		await app.click("pane.toolbar.reload");

		await app.waitFor(
			"the load stopped",
			(state) => tabOf(state, inbox)?.loading === false,
			5_000,
		);

		// A new tab: its address focused, the start page's URL selected, to
		// type over.
		await app.send("menu", { path: ["File", "New Tab"] });

		const fresh = await app.waitFor(
			"the new tab's address focused",
			(state) => state.focus === "pane.toolbar.address" && state.address !== "",
			5_000,
		);

		await app.render("toolbar-new-tab.png", 300);
		log.newTab = { address: fresh.address, toolbar: fresh.toolbar };
	} finally {
		silent.stop();
		await app.browser.close();
	}
}

// Offered 99.0.0 with no profile, as surfaces' third copy: the pill, under
// the pointer and over its X; the download under way, under the pointer;
// done (Quit and install: the fixtures' dmg holds no Pane to stage).
async function pill(
	context: Parameters<Scenario>[0],
	log: Record<string, unknown>,
) {
	const app = await launchPane(context, {
		env: {
			PANE_UPDATE_URL: context.fixtures.url("releases/latest?tag=v99.0.0"),
			PANE_UPDATE_DOWNLOADS: context.tempDir("pane-toolbar-updates-"),
			PANE_UPDATE_OPEN: "0",
		},
	});

	try {
		await app.waitFor(
			"the update offered",
			(state) => state.update.status === "available" && state.update.pill,
			15_000,
		);

		await app.render("pill-available.png", 600);

		await app.send("hoverWindow", await app.middleOf("pane.sidebar.update"));
		await app.render("pill-hover.png", 600);

		await app.send(
			"hoverWindow",
			await app.middleOf("pane.sidebar.update.dismiss"),
		);

		await app.render("pill-dismiss-hover.png", 600);
		await away(app);

		// The pill's click, a real one.
		await app.click("pane.sidebar.update");

		// 4 MB in 16 chunks, 250 ms apart.
		await app.waitFor(
			"the download under way",
			(state) =>
				state.update.status === "downloading" &&
				(state.update.progress ?? 0) >= 0.25,
			15_000,
		);

		await app.render("pill-downloading.png", 0);
		await app.send("hoverWindow", await app.middleOf("pane.sidebar.update"));
		await app.render("pill-downloading-hover.png", 400);
		await away(app);

		const done = await app.waitFor(
			"the download done",
			(state) => state.update.status === "downloaded",
			30_000,
		);

		log.downloaded = done.update;
		await app.render("pill-downloaded.png", 600);

		expect(
			done.update.label === "Quit and install" && done.update.pill,
			`downloaded: ${JSON.stringify(done.update)}`,
		);

		// Dismissed: the pill goes, and the footer gives its room back.
		await app.click("pane.sidebar.update.dismiss");

		const dismissed = await app.waitFor(
			"the pill gone",
			(state) => !state.update.pill,
			5_000,
		);

		await app.render("pill-dismissed.png", 600);
		log.dismissed = dismissed.update;
	} finally {
		await app.browser.close();
	}
}

const viewsToolbar: Scenario = async (context) => {
	const log: Record<string, unknown> = {};

	await toolbar(context, log);
	await pill(context, log);

	return log;
};

export default viewsToolbar;
