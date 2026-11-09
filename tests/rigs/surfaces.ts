import { readFileSync } from "node:fs";
import * as net from "node:net";
import { join } from "node:path";
import {
	type AppState,
	bringToFront,
	launchPane,
	type PaneApp,
	setUpPin,
	tabOf,
} from "../lib/pane";
import { poll } from "../lib/poll";
import { startHttpProxy } from "../lib/proxies";
import { waitForSheet } from "../lib/surfaces";
import { clickAt } from "../lib/traced";
import { expect, type Scenario, type ScenarioContext } from "../run";

// Every surface and state of Pane's own UI that the scenarios' renders miss
// (docs/plans/views-migration.md, B2), drawn by the app as
// `surfaces-<surface>-<state>.png`, the page left out, in the run's
// appearance (run.ts --appearance light|dark, one --out folder each). The
// same rig against the Views build gives the pairs Phase 3 compares.
//
//   bun tests/run.ts --binary <Pane.app>/Contents/MacOS/Pane --out <dir>/light \
//     --appearance light --timeout 1200 tests/rigs/surfaces.ts
//
// Three test copies, one after another, on throwaway data and the fixtures:
// the window (sidebar, a drag, toolbar, find, the switcher, every page
// dialog, the sad tab, the Extensions page, Quit Pane?), Settings and the
// Lock, and an update on offer. Nothing takes the front: what needs it is
// deferred unless the run allows it (--foreground). Text holding the fixtures' port,
// and Settings' "checked at" time, differ from run to run, and the real
// pointer over the window can add a hover.

// What no render here shows, and why; the report lists it.
const SKIPPED = {
	"delete-profile alert":
		"a SwiftUI .alert on the profile's right-click menu: no channel command opens it, and it is a sheet window, which render does not draw",
	"row menus, the Theme pop-up, the page's context menu":
		"NSMenus: windows of their own, which render (the window's host view) does not draw, opened by a right click or a press the channel does not post",
	"device chooser":
		"the Swift UI shows none: USB, HID, serial, and Bluetooth requests are declined (device-chooser)",
	"extension install prompt":
		"it installs from the Chrome Web Store, off the fixtures; extensions-menu renders it (install-popup.png)",
	"print and file panels": "system panels, windows of their own",
	"data move (Paper 07.5)":
		"needs an earlier Pane's data (PANE_EARLIER_BUILDS, files-migration-large)",
	"Restart to update":
		"needs a dmg holding a stand-in Pane to stage (update-install); the fixtures' dmg installs by hand",
	"new tab's empty address field":
		"a new tab opens https://www.google.com, off the fixtures",
	"certificate warning": "web content, which render leaves out",
	"DevTools docked":
		"a second web view in the page's place: web content, which render leaves out, so the render matched the window without them",
	"hovers (a sidebar row, the update pill's dismiss, the Lock's update action)":
		"onHover follows the pointer's tracking areas, which hoverWindow's synthetic move reached once in three runs (the pill's), and a row's never; the real pointer is the user's",
};

const PROXY_CREDENTIALS = { username: "pane", password: "proxy-secret" };

// The renders made, in order, for the report.
class Renders {
	readonly files: string[] = [];

	async take(app: PaneApp, name: string, settleMs = 400): Promise<void> {
		const file = `surfaces-${name}.png`;
		await app.render(file, settleMs);
		this.files.push(file);
	}
}

type Family = {
	context: ScenarioContext;
	app: PaneApp;
	renders: Renders;
};

// A tab at `url`, once loaded and titled, without attaching to its page (a
// debugger attached keeps Chromium from seeing a hung page).
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

async function closeTab(app: PaneApp, tab: string): Promise<void> {
	await app.send("closeTab", { tab });
	await app.waitFor("the tab closed", (state) => !tabOf(state, tab));
}

// A dialog's button, as clicked: the channel presses it, so no input goes
// to the screen.
async function answer(
	app: PaneApp,
	button: string,
	texts: string[] = [],
): Promise<void> {
	await app.send("clickSheetButton", { title: button, texts });
}

function proxyStatusOf(state: AppState, profile: string) {
	return state.profiles.find((each) => each.id === profile)?.proxyStatus;
}

// A server that takes a connection and never answers: a proxy whose test
// stays "testing" for its 10 s, and a load that never ends.
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

// The sidebar: five profiles. Work, selected and open, holds a loaded tab, a
// sleeping one (dimmed), and one loading (a server that never answers);
// Proxied is open with its proxy connected (an emerald dot); the next two
// are unloaded with a failed proxy (red) and one still testing (yellow);
// Personal is unloaded with its two tabs counted. Returns the tabs the next
// families use.
async function sidebar(
	{ context, app, renders }: Family,
	proxyPort: number,
	silentPort: number,
) {
	const { fixtures } = context;
	const work = await app.createProfile("Work", "BLUE");
	const proxied = await app.createProfile("Proxied", "EMERALD");
	const refused = await app.createProfile("Wrong proxy password", "ROSE");
	const testing = await app.createProfile("Silent proxy", "AMBER");
	const personal = await app.createProfile("Personal", "VIOLET");

	// Calendar sleeps behind Docs: a minute in the background is a second
	// here (PANE_TIME_SCALE), and sleep goes off again once it has.
	const calendar = await loadTab(
		app,
		work,
		fixtures.url("switcher.html?name=Calendar"),
	);

	const docs = await loadTab(
		app,
		work,
		fixtures.url("switcher.html?name=Docs"),
	);

	await app.send("setSleepSettings", {
		tabSleepAfterMinutes: 1,
		profileUnloadAfterMinutes: null,
		cacheBudgetMB: null,
	});

	await app.waitFor(
		"Calendar asleep",
		(state) => tabOf(state, calendar)?.loaded === false,
	);

	await app.send("setSleepSettings", {
		tabSleepAfterMinutes: null,
		profileUnloadAfterMinutes: null,
		cacheBudgetMB: null,
	});

	const home = await loadTab(app, work, fixtures.url("nav-a.html?work"));

	// The proxies, tested through the local one: the right password, and a
	// wrong one, which also asks to sign in (requests, below).
	const proxyOf = (password: string) => ({
		proxyType: "HTTP",
		host: "127.0.0.1",
		port: proxyPort,
		username: PROXY_CREDENTIALS.username,
		password,
	});

	await app.send("setProfileProxy", {
		profile: proxied,
		proxy: proxyOf(PROXY_CREDENTIALS.password),
	});

	await app.send("setProfileProxy", {
		profile: refused,
		proxy: proxyOf("wrong"),
	});

	await loadTab(
		app,
		proxied,
		"http://proxied.pane.test/switcher.html?name=Through%20the%20proxy",
	);

	await app.waitFor(
		"the proxies tested",
		(state) =>
			proxyStatusOf(state, proxied) === "connected" &&
			proxyStatusOf(state, refused) === "failed",
		30_000,
	);

	for (const name of ["Notes", "Music"]) {
		await loadTab(app, personal, fixtures.url(`switcher.html?name=${name}`));
	}

	for (const profile of [refused, testing, personal]) {
		await app.send("unloadProfile", { profile });
	}

	// Inbox goes to a server that never answers, and is left loading behind
	// Home. (The fixtures' server drops a request held past 10 s.)
	const held = await loadTab(
		app,
		work,
		fixtures.url("switcher.html?name=Inbox"),
	);

	await app.send("navigate", { input: `http://127.0.0.1:${silentPort}/inbox` });

	await app.waitFor(
		"the held load",
		(state) => tabOf(state, held)?.loading === true,
		5_000,
	);

	await app.send("select", { tab: home });

	// Last, the silent proxy: its test runs 10 s.
	await app.send("setProfileProxy", {
		profile: testing,
		proxy: { proxyType: "HTTP", host: "127.0.0.1", port: silentPort },
	});

	await app.waitFor(
		"the silent proxy testing",
		(state) => proxyStatusOf(state, testing) === "testing",
		5_000,
	);

	await renders.take(app, "window-two-profiles", 300);

	return { work, refused, home, docs, held };
}

// The toolbar: the held load selected (the bar at its start, Stop in
// Reload's place), then five seconds on (the slow mode's shimmer); the
// address field focused, its URL selected; typed text, which needs the key
// window.
async function toolbar(
	{ context, app, renders }: Family,
	tabs: { home: string; docs: string; held: string },
) {
	await app.send("select", { tab: tabs.held });

	await app.waitFor(
		"the bar shown",
		(state) => state.progressBar.started && state.progressBar.alpha === 1,
		5_000,
	);

	await renders.take(app, "toolbar-loading-start", 150);

	await app.waitFor(
		"the slow mode",
		(state) => state.progressBar.shimmer,
		10_000,
	);

	await renders.take(app, "toolbar-loading-slow", 150);
	await closeTab(app, tabs.held);

	await app.send("select", { tab: tabs.home });
	await app.send("menu", { path: ["Navigate", "Focus Address Bar"] });

	await app.waitFor(
		"the address field focused",
		(state) => state.focus === "pane.toolbar.address",
		5_000,
	);

	await renders.take(app, "toolbar-address-focused");

	// Keys reach a field only in the key window.
	if (await bringToFrontIfAllowed(context, app, "the address field typed in")) {
		for (const key of "quart") {
			await app.send("key", { key });
		}

		await app.waitFor(
			"the typed text",
			(state) => state.address === "quart",
			5_000,
		);

		await renders.take(app, "toolbar-address-typed");
	}

	// Another tab and back gives the page the keys again.
	await app.send("select", { tab: tabs.docs });
	await app.send("select", { tab: tabs.home });

	await app.waitFor(
		"the address field left",
		(state) => state.focus !== "pane.toolbar.address",
		5_000,
	);
}

// Only with --foreground does the app take the front; otherwise the state
// is deferred.
async function bringToFrontIfAllowed(
	context: ScenarioContext,
	app: PaneApp,
	check: string,
): Promise<boolean> {
	if (!context.foreground) {
		context.defer(`${check}: it needs Pane in front (run.ts --foreground)`);

		return false;
	}

	return bringToFront(context, app, check);
}

// Find in page: five matches, the first current; then a word with none.
async function findBar({ context, app, renders }: Family, work: string) {
	const tab = await loadTab(app, work, context.fixtures.url("find.html"));

	await app.send("find", { text: "needle" });

	await app.waitFor(
		"5 matches",
		(state) => state.find.matches === 5 && state.find.activeMatch === 1,
	);

	await renders.take(app, "find-matches");

	await app.send("find", { text: "absent-word" });

	await app.waitFor(
		"no match",
		(state) => state.find.visible && state.find.matches === 0,
	);

	await renders.take(app, "find-no-match");
	await app.send("closeFind");
	await closeTab(app, tab);
}

// The tab switcher (⌃⇥), open on the previous tab.
async function tabSwitcher({ app, renders }: Family) {
	await app.send("switcher", { action: "next" });
	await app.waitFor("the switcher", (state) => state.switcher.visible);
	await renders.take(app, "switcher-open");
	await app.send("switcher", { action: "cancel" });
	await app.waitFor("the switcher gone", (state) => !state.switcher.visible);
}

// A page's own dialogs: alert, confirm, prompt (its field filled with the
// default), beforeunload on a reload and on a close, and a form's
// resubmission.
async function pageDialogs({ context, app, renders }: Family, work: string) {
	const { fixtures } = context;

	const { tab, page } = await app.openPage(
		work,
		fixtures.url("dialogs.html?surfaces"),
	);

	const gone = () =>
		app.waitFor("the dialog answered", (state) => state.sheet === null);

	for (const [run, name, button] of [
		["runAlert()", "dialog-alert", "OK"],
		["runConfirm()", "dialog-confirm", "Cancel"],
		["runPrompt()", "dialog-prompt", "Cancel"],
	] as const) {
		await page.evaluate(run);
		await waitForSheet(app, "javaScript");
		await renders.take(app, name);
		await answer(app, button);
		await gone();
	}

	// Chrome asks before unloading only a page the user interacted with.
	await page.evaluate("armBeforeUnload()");
	await clickAt((method, params) => page.send(method, params), 20, 20);

	await app.send("reload");
	const reloading = await waitForSheet(app, "javaScript");
	expect(reloading.title === "Reload site?", `sheet ${reloading.title}`);
	await renders.take(app, "dialog-reload-site");
	await answer(app, "Cancel");
	await gone();

	await app.send("closeTab", { tab });
	const leaving = await waitForSheet(app, "javaScript");
	expect(leaving.title === "Leave site?", `sheet ${leaving.title}`);
	await renders.take(app, "dialog-leave-site");
	await answer(app, "Leave");
	await app.waitFor("the tab closed", (state) => !tabOf(state, tab));

	// A posted page, reloaded.
	const echo = fixtures.url("echo");

	const { tab: form, page: formPage } = await app.openPage(
		work,
		fixtures.url("form.html"),
	);

	await formPage.evaluate("document.getElementById('form').submit()");

	await app.waitFor(
		"the posted page",
		(state) => tabOf(state, form)?.url === echo && !tabOf(state, form)?.loading,
	);

	await app.send("reload");
	const resubmit = await waitForSheet(app, "javaScript");

	expect(
		resubmit.title === "Confirm Form Resubmission",
		`sheet ${resubmit.title}`,
	);

	await renders.take(app, "dialog-form-resubmission");
	await answer(app, "Cancel");
	await gone();
	await closeTab(app, form);
}

// What a site asks of the user: a permission (notifications), another app
// (a maps: link), and a sign-in, from the server, again once refused (the
// Swift dialog has no error line: it asks again, empty), and from a proxy.
async function requests(
	{ context, app, renders }: Family,
	work: string,
	refused: string,
) {
	const { fixtures } = context;

	const { tab: asking, page: permissions } = await app.openPage(
		work,
		fixtures.url("permissions.html"),
	);

	await permissions.gesture("askNotifications()");
	await waitForSheet(app, "permission");
	await renders.take(app, "dialog-permission");
	await answer(app, "Don’t Allow");
	await closeTab(app, asking);

	// Cancel only: Open would start Maps on this Mac.
	const { tab: linking, page: external } = await app.openPage(
		work,
		fixtures.url("external.html"),
	);

	const link = await external.evaluate<{ x: number; y: number }>(
		"(() => { const r = document.getElementById('mail').getBoundingClientRect(); return { x: r.left + 5, y: r.top + 5 }; })()",
	);

	await clickAt(
		(method, params) => external.send(method, params),
		link.x,
		link.y,
	);

	await waitForSheet(app, "externalProtocol");
	await renders.take(app, "dialog-external-app");
	await answer(app, "Cancel");
	await closeTab(app, linking);

	const signIn = await app.openTab(work, fixtures.url("auth"));
	await waitForSheet(app, "authentication");
	await renders.take(app, "dialog-sign-in");

	await answer(app, "Sign In", ["pane", "wrong"]);

	await app
		.waitFor("the sign-in sent", (state) => state.sheet === null, 3_000)
		.catch(() => {});

	await waitForSheet(app, "authentication");
	await renders.take(app, "dialog-sign-in-refused");
	await answer(app, "Cancel");

	await app.waitFor(
		"the refusal",
		(state) => tabOf(state, signIn)?.loading === false,
	);

	await closeTab(app, signIn);

	// The proxy turns the stored password down, and Pane asks.
	const proxied = await app.openTab(
		refused,
		"http://sign-in.pane.test/nav-b.html",
	);

	const proxySheet = await waitForSheet(app, "authentication", 30_000);

	expect(
		proxySheet.message?.startsWith("The proxy"),
		`sheet ${proxySheet.message}`,
	);

	await renders.take(app, "dialog-sign-in-proxy");
	await answer(app, "Cancel");
	await closeTab(app, proxied);
	await app.send("unloadProfile", { profile: refused });
}

// Pages that stopped: Page Unresponsive over a page in an endless loop, and
// the sad tab of a page that crashed twice in a minute.
async function brokenPages(
	{ context, app, renders }: Family,
	work: string,
	crashes: () => number,
) {
	const { fixtures } = context;
	const hung = await app.openTab(work, fixtures.url("hang.html?auto"));

	// It names itself Hanging, then loops 50 ms later; a key typed into it
	// then goes unanswered. One that lands before the loop is answered, so
	// a key goes again while no dialog comes.
	await app.waitFor(
		"the page hung",
		(state) => tabOf(state, hung)?.title === "Hanging",
	);

	await poll(
		"Page Unresponsive",
		async () => {
			await Bun.sleep(500);
			await app.send("typeInPage", { key: "a" });

			return waitForSheet(app, "unresponsive", 25_000).catch(() => null);
		},
		75_000,
		0,
	);

	await renders.take(app, "dialog-hung-page");
	await answer(app, "Exit Page");
	await closeTab(app, hung);

	const url = fixtures.url("crash.html?sad");
	const crashing = await app.openTab(work, url);
	const before = crashes();

	for (let crash = before + 1; crash <= before + 2; crash++) {
		const page = await app.page(crashing, url);
		// The renderer goes before it can answer.
		page.send("Page.crash").catch(() => {});
		await poll(`crash ${crash}`, () => crashes() >= crash, 15_000, 150);
	}

	await app.waitFor("the sad tab", (state) =>
		state.crashedTabs.includes(crashing),
	);

	await renders.take(app, "sad-tab-crashed");
	await closeTab(app, crashing);
}

// A tab dragged down its profile, through the event queue as the trackpad's
// moves (sidebar-reorder): the copy rides the pointer, and the row below
// already stands where the drop would put it. Then back, and let go where
// it started, which keeps the order.
async function reorderDrag(
	{ app, renders }: Family,
	work: string,
	tab: string,
) {
	const tabsOf = (state: AppState) =>
		state.profiles
			.find((each) => each.id === work)
			?.tabs.map((each) => each.id) ?? [];

	const before = tabsOf(await app.state());

	// Tab rows: 32 points, 4 apart, under Work's 36-point header, from the
	// list's top at 43.
	const y = 43 + 36 + 4 + before.indexOf(tab) * 36 + 16;
	const path = [4, 12, 24, 36, 44].map((dy) => y + dy);

	const move = async (points: number[]) => {
		for (const to of points) {
			await app.send("pointerWindow", { phase: "drag", x: 100, y: to });
			await Bun.sleep(40);
		}
	};

	await app.send("pointerWindow", { phase: "down", x: 100, y });
	await move(path);
	// The rows slide aside on a 0.28-second spring.
	await renders.take(app, "sidebar-reorder-dragging", 500);
	await move([...path].reverse().concat(y));
	await app.send("pointerWindow", { phase: "up", x: 100, y });
	await Bun.sleep(400);
	const after = tabsOf(await app.state());

	expect(
		after.join() === before.join(),
		`the drag back moved the tabs: ${before} → ${after}`,
	);
}

// The Extensions page with no extension in the profile, or no profile.
async function extensionsPage({ app, renders }: Family, state: string) {
	await app.send("showPage", { page: "extensions" });

	await app.waitFor(
		"the Extensions page",
		(shown) => shown.page === "extensions",
	);

	await renders.take(app, `extensions-page-${state}`);
	await app.send("showPage", { page: "browser" });
}

// More rows than the sidebar shows: scrolled to the last tab, which fades
// the list's top edge, then a little back, which fades both.
async function sidebarOverflow({ context, app, renders }: Family) {
	const reading = await app.createProfile("Reading list", "TEAL");

	for (let article = 1; article <= 12; article++) {
		await loadTab(
			app,
			reading,
			context.fixtures.url(`switcher.html?name=Article%20${article}`),
		);
	}

	await renders.take(app, "sidebar-overflow-end", 600);
	await app.send("scrollWindow", { x: 130, y: 400, dy: 160 });
	await renders.take(app, "sidebar-overflow-middle", 600);
	await app.send("unloadProfile", { profile: reading });
}

// A page's fullscreen: the window without the panel, and the hint to press
// Esc. Since Chromium 157 it starts only from a focused page in the key
// window (the fullscreen scenario's steps).
async function fullscreenHint({ context, app, renders }: Family, work: string) {
	const check = "the fullscreen hint";

	if (!(await bringToFrontIfAllowed(context, app, check))) {
		return;
	}

	const { tab, page } = await app.openPage(
		work,
		context.fixtures.url("fullscreen.html"),
	);

	await app.send("clickPage", { x: 20, y: 20 });

	await app.waitFor(
		"the page focused",
		(state) => state.focus === "pane.content",
	);

	await page.gesture("document.documentElement.requestFullscreen()", true);

	await app.waitFor(
		"the hint",
		(state) => state.pageFullscreen && state.exclusiveAccessHint !== null,
	);

	await renders.take(app, "fullscreen-hint", 600);

	await poll(
		"fullscreen to end",
		async () => {
			await app.send("key", { key: "\u001b" });

			return app
				.waitFor("fullscreen to end", (state) => !state.pageFullscreen, 1_000)
				.catch(() => null);
		},
		20_000,
		0,
	);

	await app.waitFor("the window back", (state) => !state.window.fullScreen);
	await closeTab(app, tab);
}

// Quit Pane?, as Pane › Quit Pane shows it. Last: a second Quit quits.
async function quitDialog({ app, renders }: Family) {
	await app.send("menu", { path: ["Pane", "Quit Pane"] });
	await app.waitFor("Quit Pane?", (state) => state.quitConfirmation);
	await renders.take(app, "quit-confirm");
}

// The first copy: the window and everything a page brings up. A minute is a
// second (for a tab to sleep), with every timer of Sleep and Unload off but
// that one moment.
async function browsing(context: ScenarioContext, renders: Renders) {
	const fixturePort = Number(new URL(context.fixtures.origin).port);
	const proxy = await startHttpProxy(fixturePort, PROXY_CREDENTIALS);
	const silent = await silentServer();
	const trace = join(context.tempDir("pane-surfaces-trace-"), "trace.jsonl");

	try {
		const app = await launchPane(context, {
			env: {
				PANE_TIME_SCALE: "60",
				PANE_PROXY_TEST_URL: "http://check.pane.test/ip",
				PANE_ENGINE_TRACE: trace,
			},
		});

		await app.send("setSleepSettings", {
			tabSleepAfterMinutes: null,
			profileUnloadAfterMinutes: null,
			cacheBudgetMB: null,
		});

		// The renderers that ended, from the engine's trace.
		const crashes = () => {
			try {
				return readFileSync(trace, "utf8")
					.split("\n")
					.filter((line) =>
						line.includes("PaneWebContentsDelegate.rendererDidTerminate"),
					).length;
			} catch {
				return 0;
			}
		};

		const family = { context, app, renders };
		const tabs = await sidebar(family, proxy.port, silent.port);
		await toolbar(family, tabs);
		await findBar(family, tabs.work);
		await tabSwitcher(family);
		await pageDialogs(family, tabs.work);
		await reorderDrag(family, tabs.work, tabs.docs);
		await requests(family, tabs.work, tabs.refused);
		await brokenPages(family, tabs.work, crashes);
		await app.send("select", { tab: tabs.home });
		await extensionsPage(family, "empty");
		await fullscreenHint(family, tabs.work);
		await sidebarOverflow(family);
		await app.send("select", { tab: tabs.home });
		await quitDialog(family);
		await app.browser.close();
	} finally {
		proxy.stop();
		silent.stop();
	}
}

// Settings, from the top and scrolled to its end; Check for updates while
// it runs; the App icon picker, on Automatic and on Dusk, and the row
// showing Dusk.
async function settings({ app, renders }: Family) {
	await app.send("showPage", { page: "settings" });

	await app.waitFor(
		"Settings, after the launch's check",
		(state) =>
			state.page === "settings" &&
			state.update.checked &&
			state.update.status === "idle",
		15_000,
	);

	await renders.take(app, "settings-top");
	await scrollSettingsToEnd(app);
	await renders.take(app, "settings-end");

	await app.send("checkUpdates");

	await app.waitFor(
		"the check running",
		(state) => state.update.status === "checking",
		5_000,
	);

	await renders.take(app, "settings-updates-checking", 100);

	await app.waitFor(
		"the check done",
		(state) => state.update.status === "idle",
		15_000,
	);

	await app.send("appIconPicker", { open: true });
	await app.waitFor("the picker", (state) => state.appIconPicker, 5_000);
	await renders.take(app, "app-icon-picker-automatic");

	await app.send("appIcon", { appIcon: "dusk" });
	await app.waitFor("Dusk", (state) => state.dockIcon === "dusk", 5_000);
	await renders.take(app, "app-icon-picker-dusk");

	await app.send("appIconPicker", { open: false });

	await app.waitFor(
		"the picker closed",
		(state) => !state.appIconPicker,
		5_000,
	);

	await renders.take(app, "settings-app-icon-dusk");
	await app.send("appIcon", { appIcon: "automatic" });
}

async function scrollSettingsToEnd(app: PaneApp) {
	await app.send("scrollWindow", { x: 700, y: 400, dy: -2_000 });
}

// The PIN screen from Settings: setting one up (empty, typed, long enough to
// continue, confirming, and two that differ), changing it (the current, then
// the new one twice), and removing it (the current).
async function pinScreens({ app, renders }: Family) {
	const screen = (state: AppState) => state.security.screen;

	const keys = async (
		typed: string,
		description: string,
		until: (state: AppState) => boolean,
	) => {
		await app.send("pinKeys", { keys: typed });
		await app.waitFor(description, until, 10_000);
	};

	await app.send("showPinScreen", { mode: "setup" });

	await app.waitFor(
		"the PIN screen",
		(state) => screen(state)?.mode === "setup",
	);

	await renders.take(app, "pin-setup-empty", 800);

	await keys("12", "two digits", (state) => screen(state)?.entered === 2);
	await renders.take(app, "pin-setup-typing");

	await keys("34", "four digits", (state) => screen(state)?.entered === 4);
	await renders.take(app, "pin-setup-continue");

	await keys(">", "confirming", (state) => screen(state)?.step === "confirm");
	await renders.take(app, "pin-setup-confirm");

	await keys(
		"1235",
		"the mismatch, shaken",
		(state) =>
			screen(state)?.error === "PINs don't match" && !screen(state)?.busy,
	);

	await renders.take(app, "pin-setup-mismatch", 500);

	await keys(
		"1234",
		"the PIN set up",
		(state) => screen(state) === null && state.security.pinLength === 4,
	);

	await scrollSettingsToEnd(app);
	await renders.take(app, "settings-pin-set", 800);

	await app.send("showPinScreen", { mode: "change" });

	await app.waitFor(
		"the PIN screen",
		(state) => screen(state)?.mode === "change",
	);

	await renders.take(app, "pin-change-current", 800);

	await keys(
		"1234",
		"the new PIN asked",
		(state) => screen(state)?.step === "enter",
	);

	await renders.take(app, "pin-change-new");

	await keys(
		"5678>",
		"the new PIN confirmed",
		(state) => screen(state)?.step === "confirm",
	);

	await renders.take(app, "pin-change-confirm");

	await keys(
		"5678",
		"the PIN changed",
		(state) => screen(state) === null && state.security.pinLength === 4,
	);

	await app.send("showPinScreen", { mode: "remove" });

	await app.waitFor(
		"the PIN screen",
		(state) => screen(state)?.mode === "remove",
	);

	await renders.take(app, "pin-remove-current", 800);

	await keys(
		"5678",
		"the PIN removed",
		(state) => screen(state) === null && state.security.pinLength === null,
	);
}

// The Lock (Lock Pane, Paper 07.9): at rest, two digits in, a wrong PIN
// (attempts left), and four (the last attempt). The fifth would erase
// everything; the right PIN opens it.
async function lockScreens({ app, renders }: Family) {
	await setUpPin(app, "1234");
	await app.send("lockNow");

	await app.waitFor(
		"the Lock",
		(state) =>
			state.security.locked && state.security.screen?.mode === "unlock",
	);

	await renders.take(app, "lock-unlock", 800);

	await app.send("pinKeys", { keys: "12" });

	await app.waitFor(
		"two digits",
		(state) => state.security.screen?.entered === 2,
	);

	await renders.take(app, "lock-typing");
	await app.send("pinKeys", { keys: "<<" });

	for (let attempt = 1; attempt <= 4; attempt++) {
		await app.send("pinKeys", { keys: "0000" });

		await app.waitFor(
			`wrong PIN ${attempt}`,
			(state) =>
				state.security.failedAttempts === attempt &&
				state.security.screen?.entered === 0 &&
				!state.security.screen?.busy,
			10_000,
		);

		if (attempt === 1) {
			await renders.take(app, "lock-wrong-pin", 500);
		}
	}

	await renders.take(app, "lock-last-attempt", 500);
	await app.send("pinKeys", { keys: "1234" });

	await app.waitFor(
		"unlocked",
		(state) => !state.security.locked && state.security.screen === null,
		15_000,
	);
}

// The second copy: Settings and the Lock, at a profile with one tab.
async function settingsAndLock(context: ScenarioContext, renders: Renders) {
	const app = await launchPane(context);
	const family = { context, app, renders };
	const work = await app.createProfile("Work", "BLUE");
	await loadTab(app, work, context.fixtures.url("nav-a.html?settings"));
	await settings(family);
	await pinScreens(family);
	await lockScreens(family);
	await app.browser.close();
}

// The third copy, offered 99.0.0, with no profile: the sidebar's pill, and
// Settings' Updates; the Lock with the update beside its status; then the
// download under way, and done (Quit and install: the fixtures' dmg holds no
// Pane to stage). The Extensions page with no profile too.
async function updates(context: ScenarioContext, renders: Renders) {
	const app = await launchPane(context, {
		env: {
			PANE_UPDATE_URL: context.fixtures.url("releases/latest?tag=v99.0.0"),
			PANE_UPDATE_DOWNLOADS: context.tempDir("pane-surfaces-updates-"),
			PANE_UPDATE_OPEN: "0",
		},
	});

	const family = { context, app, renders };

	await app.waitFor(
		"the update offered",
		(state) => state.update.status === "available" && state.update.pill,
		15_000,
	);

	await renders.take(app, "update-pill-available");

	await extensionsPage(family, "no-profile");

	await app.send("showPage", { page: "settings" });
	await app.waitFor("Settings", (state) => state.page === "settings");
	await scrollSettingsToEnd(app);
	await renders.take(app, "updates-available");

	await setUpPin(app, "1234");
	await app.send("lockNow");
	await app.waitFor("the Lock", (state) => state.security.locked);
	await renders.take(app, "lock-update-available", 800);
	await app.send("pinKeys", { keys: "1234" });
	await app.waitFor("unlocked", (state) => !state.security.locked, 15_000);

	await app.send("showPage", { page: "settings" });
	await app.waitFor("Settings", (state) => state.page === "settings");
	await scrollSettingsToEnd(app);
	await app.send("downloadUpdate");

	// 4 MB in 16 chunks, 250 ms apart.
	await app.waitFor(
		"the download under way",
		(state) =>
			state.update.status === "downloading" &&
			(state.update.progress ?? 0) >= 0.25,
		15_000,
	);

	await renders.take(app, "updates-downloading", 0);

	await app.waitFor(
		"the download done",
		(state) => state.update.status === "downloaded",
		30_000,
	);

	await renders.take(app, "updates-downloaded");
	await app.browser.close();
}

const surfaces: Scenario = async (context) => {
	const renders = new Renders();

	await browsing(context, renders);
	await settingsAndLock(context, renders);
	await updates(context, renders);

	return { renders: renders.files, skipped: SKIPPED };
};

export default surfaces;
