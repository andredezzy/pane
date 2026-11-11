// Launches the Pane app with its data in a throwaway (or given) directory and
// drives it through the automation channel (PANE_AUTOMATION_DIR, see
// app/Pane/Automation/Automation.swift).
import {
	existsSync,
	mkdirSync,
	readFileSync,
	renameSync,
	rmSync,
	watch,
	writeFileSync,
} from "node:fs";
import { dirname, join } from "node:path";
import type { ScenarioContext } from "../run";
import type { Browser } from "./browser";
import { openPage, type Page } from "./cdp";
import { isScreenLocked } from "./cua";
import { targets } from "./targets";

export type TabState = {
	id: string;
	url: string;
	title: string;
	hasFavicon: boolean;
	loaded: boolean;
	loading: boolean;
	canGoBack: boolean;
	canGoForward: boolean;
};

type ProfileState = {
	id: string;
	name: string;
	color: string;
	expanded: boolean;
	// The proxy's last connection test, for a proxied profile.
	proxyStatus: "testing" | "connected" | "failed" | null;
	// What the engine applied, once the profile is open.
	keepLoaded: boolean;
	savesHistory: boolean;
	// What the engine has, once the profile is open.
	engineSavesHistory: boolean | null;
	// Each cookie but its value, once read with readCookies. expires is in
	// seconds since 1970, null for a session cookie.
	cookies:
		| {
				name: string;
				domain: string;
				path: string;
				secure: boolean;
				httpOnly: boolean;
				sameSite: "unspecified" | "none" | "lax" | "strict";
				expires: number | null;
		  }[]
		| null;
	// The HTTP cache's bytes, once read with readCacheSize.
	cacheSize: number | null;
	tabs: TabState[];
};

type DownloadState = {
	id: string;
	url: string;
	path: string | null;
	receivedBytes: number;
	state: "inProgress" | "complete" | "cancelled" | "interrupted";
};

type SecurityState = {
	pinLength: number | null;
	failedAttempts: number;
	locked: boolean;
	// The panic wipe's erase, while it runs, and what it could not erase.
	wipe: { erasing: boolean; failures: string[] };
	// The data key's side (ADR 0113): closed, "moving <share>", or open; the
	// volume attached; and why it would not attach.
	data: {
		phase: string;
		attached: boolean;
		failure: string | null;
		// When the attach started and ended, in ms since 1970.
		attachedAt: { started: number; ended: number } | null;
	};
	// The PIN screen, while it shows.
	screen: {
		mode: "setup" | "change" | "remove" | "unlock";
		step: "verify" | "enter" | "confirm";
		title: string | null;
		dots: number;
		entered: number;
		busy: boolean;
		error: string | null;
	} | null;
};

type UpdateState = {
	status: "idle" | "checking" | "available" | "downloading" | "downloaded";
	available: string | null;
	progress: number | null;
	pill: boolean;
	// What the pill and the lock screen say.
	label: string;
	checked: boolean;
	// The downloaded update, staged beside the app to install at quit (ADR 0092).
	installsItself: boolean;
};

// An item of a page's right-click menu; a submenu lists its own. The Views
// build's, Chrome's own menu, name each item's command too.
export type MenuItemState = {
	title: string;
	command?: number;
	enabled: boolean;
	checked: boolean;
	submenu?: MenuItemState[];
};

export type AppState = {
	// The profile sheet: "new", or the edited profile's id.
	profileSheet: string | null;
	quitConfirmation: boolean;
	// The toolbar puzzle's menu is open.
	extensionsMenu: boolean;
	sidebarWidth: number;
	// The extension whose more menu is open beside it.
	extensionMoreMenu: string | null;
	// The Mac's WebGL strings, from Chrome's GPU info.
	hostWebGL: { vendor: string; renderer: string } | null;
	update: UpdateState;
	security: SecurityState;
	profiles: ProfileState[];
	activeTabID: string | null;
	activeProfileID: string | null;
	page: "browser" | "settings";
	theme: string;
	appearance: string;
	// The App icon setting, and the window the Dock shows for it.
	appIcon: string;
	dockIcon: string;
	// Settings' App icon picker, open.
	appIconPicker: boolean;
	find: {
		visible: boolean;
		text: string;
		matches: number;
		activeMatch: number;
	};
	// The tab switcher: its selection, counting down through its groups, and
	// each group's profile and tabs.
	switcher: {
		visible: boolean;
		selectedIndex: number;
		groups: { profileID: string; name: string; tabs: string[] }[];
	};
	downloads: DownloadState[];
	// The address field's text.
	address: string;
	window: {
		title: string;
		visible: boolean;
		fullScreen: boolean;
		frame: string;
		key: boolean;
		trafficLightFrames: number[][];
	};
	closedTabs: number;
	pendingProfileDeletions: number;
	focus: string;
	sheet: {
		kind: string;
		tabID?: string | null;
		title?: string;
		message?: string;
		buttons?: string[];
		fields?: string[];
		panel?: "open" | "save";
		// An extension's removal popup: Report abuse, ticked.
		reportAbuse?: boolean;
	} | null;
	pendingRequests: number;
	contextMenu: MenuItemState[] | null;
	// How many menus the page has shown: a new one counts up.
	contextMenusShown: number;
	modal: { title: string; buttons: string[] } | null;
	nativeWindows: {
		role: string;
		class: string;
		title: string;
		frame: string;
		// The window's number, for `screencapture -l` (the Views build's).
		number?: number;
	}[];
	// The alert the window shows as a sheet (the Views build's): its title,
	// its message, and its buttons from left to right.
	alert?: { title: string; message: string; buttons: string[] } | null;
	pageFullscreen: boolean;
	// The toolbar as drawn (the Views build's): each control by its view's
	// name, whether it takes presses, shows, and what it reads as; and
	// whether the address shows the page's domain and title over its text.
	toolbar?: {
		controls: Record<
			string,
			{ enabled: boolean; visible: boolean; name: string }
		>;
		addressLabel: boolean;
	};
	// The address bar's load progress as drawn: started, the share filled,
	// the opacity, and whether the slow mode's shimmer runs.
	progressBar: {
		started: boolean;
		progress: number;
		alpha: number;
		shimmer: boolean;
	};
	exclusiveAccessHint: string | null;
	crashedTabs: string[];
	pictureInPictureTabs: string[];
	devToolsTabs: string[];
	extensions: Record<string, ExtensionState[]>;
	extensionPopup: {
		extension: string;
		open: boolean;
		width: number;
		height: number;
		// It shows above its button, where the screen leaves more room.
		above?: boolean;
	} | null;
};

export type ExtensionState = {
	id: string;
	name: string;
	version: string;
	enabled: boolean;
	pinned: boolean;
	actionTitle: string;
	// The icon draws something (it can be there and blank).
	iconVisible: boolean;
};

type Reply = { ok: boolean; result?: unknown; error?: string; state: AppState };

// What a press at a point of the window would reach, asked of the window
// without input (hitTest): the view Views would hand it, by its id or the
// nearest one around it ("pane.content"), and who takes it: the page, Pane's
// views, the frame, which moves the window ("drag"), or the frame's own
// controls ("other": the traffic lights).
export type Hit = {
	handler: string;
	view: string;
	target: "page" | "views" | "drag" | "other";
	micros: number;
};

let nextCommand = 0;

// Whether `file` exists by the deadline: its folder watched, so a reply is
// read the moment the app moves it into place, with a slower look behind the
// watch in case an event goes missing.
function fileAppears(file: string, timeoutMs: number): Promise<boolean> {
	return new Promise((resolve) => {
		let done = false;

		const finish = (found: boolean) => {
			if (done) {
				return;
			}

			done = true;
			watcher.close();
			clearInterval(look);
			clearTimeout(deadline);
			resolve(found);
		};

		const seen = () => {
			if (existsSync(file)) {
				finish(true);
			}
		};

		const watcher = watch(dirname(file), seen);
		const look = setInterval(seen, 250);
		const deadline = setTimeout(() => finish(existsSync(file)), timeoutMs);
		seen();
	});
}

export class PaneApp {
	constructor(
		readonly browser: Browser,
		readonly dataDir: string,
		readonly automationDir: string,
		// Where a stuck app's sample goes: the run's output.
		private readonly out: string,
	) {}

	async send<T = unknown>(
		command: string,
		args: Record<string, unknown> = {},
	): Promise<{ result: T; state: AppState }> {
		const name = `${String(Date.now()).padStart(15, "0")}-${String(nextCommand++).padStart(6, "0")}.json`;
		const inbox = join(this.automationDir, "in");
		const reply = join(this.automationDir, "out", name);
		const temporary = join(this.automationDir, `${name}.tmp`);

		writeFileSync(temporary, JSON.stringify({ command, ...args }));
		renameSync(temporary, join(inbox, name));

		if (await fileAppears(reply, 15_000)) {
			const parsed = JSON.parse(readFileSync(reply, "utf8")) as Reply;
			rmSync(reply, { force: true });

			if (!parsed.ok) {
				throw new Error(`${command}: ${parsed.error}`);
			}

			return { result: parsed.result as T, state: parsed.state };
		}

		// A stuck app: record where its main thread is before failing.
		const sampleFile = join(this.out, `pane-hang-${command}-${Date.now()}.txt`);

		await Bun.$`sample ${this.browser.pid} 2 -file ${sampleFile}`
			.quiet()
			.nothrow();

		throw new Error(
			`${command}: no reply from the app (sample: ${sampleFile})`,
		);
	}

	async state(): Promise<AppState> {
		return (await this.send("state")).state;
	}

	async hitTest(x: number, y: number): Promise<Hit> {
		return (await this.send<Hit>("hitTest", { x, y })).result;
	}

	// Where the sidebar's and the toolbar's parts are, each shown one by name
	// ("pane.sidebar.profiles", "pane.tab.<id>", "pane.toolbar.back"), from
	// the window's top left: x, y, width, height. A row scrolled out of view
	// lies outside the list's.
	async frames(prefix = "pane."): Promise<Record<string, number[]>> {
		return (await this.send<Record<string, number[]>>("frames", { prefix }))
			.result;
	}

	// A real click at the middle of a part shown by that name (frames), as a
	// press of it reaches it.
	async click(name: string) {
		const [x = 0, y = 0, width = 0, height = 0] =
			(await this.frames(name))[name] ?? [];

		if (width === 0 || height === 0) {
			throw new Error(`click: nothing named ${name} shows`);
		}

		await this.send("clickWindow", { x: x + width / 2, y: y + height / 2 });
	}

	// Polls the app's state until `predicate` holds.
	async waitFor(
		description: string,
		predicate: (state: AppState) => boolean,
		timeoutMs = 20_000,
	): Promise<AppState> {
		const deadline = Date.now() + timeoutMs;
		let state = await this.state();

		while (!predicate(state)) {
			if (Date.now() > deadline) {
				throw new Error(
					`timed out waiting for ${description}; state: ${JSON.stringify(state)}`,
				);
			}

			await Bun.sleep(150);
			state = await this.state();
		}

		return state;
	}

	// The state once `watched` has held still for `quietMs`: for what may or
	// may not come (an extension's welcome tab), which nothing announces.
	async settled(
		description: string,
		watched: (state: AppState) => unknown,
		quietMs = 1_000,
		timeoutMs = 15_000,
	): Promise<AppState> {
		const deadline = Date.now() + timeoutMs;
		let state = await this.state();
		let value = JSON.stringify(watched(state));
		let since = Date.now();

		while (Date.now() - since < quietMs) {
			if (Date.now() > deadline) {
				throw new Error(`${description} never settled: ${value}`);
			}

			await Bun.sleep(150);
			state = await this.state();
			const next = JSON.stringify(watched(state));

			if (next !== value) {
				value = next;
				since = Date.now();
			}
		}

		return state;
	}

	async createProfile(name: string, color = "BLUE"): Promise<string> {
		return (await this.send<string>("createProfile", { name, color })).result;
	}

	async openTab(profile: string, url?: string): Promise<string> {
		return (await this.send<string>("openTab", { profile, url })).result;
	}

	// Waits for the tab to finish loading `url` and attaches CDP to its page.
	// Scenarios give every tab a distinct URL, so the URL names the target.
	async page(tabID: string, url: string): Promise<Page> {
		await this.waitFor(
			`tab ${tabID} at ${url}`,
			(state) =>
				tabOf(state, tabID)?.url === url &&
				tabOf(state, tabID)?.loading === false,
		);

		const deadline = Date.now() + 30_000;

		while (Date.now() < deadline) {
			const matching = (await targets(this.browser.cdp)).filter(
				(target) => target.type === "page" && target.url === url,
			);

			if (matching.length > 1) {
				throw new Error(`several page targets at ${url}`);
			}

			if (matching[0]) {
				// The target shows a pending URL before the page commits; wait
				// for the page itself to be there.
				const page = await openPage(
					this.browser.cdp,
					"about:blank",
					matching[0].targetId,
				);

				while (Date.now() < deadline) {
					const href = await page
						.evaluate<string>("location.href")
						.catch(() => "");

					if (href === url) {
						return page;
					}

					await Bun.sleep(150);
				}

				throw new Error(`the page at ${url} never committed`);
			}

			await Bun.sleep(250);
		}

		throw new Error(`no page target for tab ${tabID} at ${url}`);
	}

	// A tab at `url`, once loaded, and its page.
	async openPage(
		profile: string,
		url: string,
	): Promise<{ tab: string; page: Page }> {
		const tab = await this.openTab(profile, url);

		return { tab, page: await this.page(tab, url) };
	}

	// Draws the window to `file` in the run's output, a moment after what
	// changed has settled.
	async render(file: string, settleMs = 300): Promise<void> {
		await Bun.sleep(settleMs);
		await this.send("render", { path: join(this.out, file) });
	}

	// Draws an action's popup, its bubble from its own window, to `file`
	// (the Views build's; the page itself is left out, as in render).
	async renderPopup(file: string, settleMs = 300): Promise<void> {
		await Bun.sleep(settleMs);
		await this.send("render", { path: join(this.out, file), target: "popup" });
	}

	async quit(): Promise<void> {
		await this.send("quit").catch(() => {});
	}
}

export function tabOf(state: AppState, tabID: string): TabState | undefined {
	return state.profiles
		.flatMap((profile) => profile.tabs)
		.find((tab) => tab.id === tabID);
}

// The traffic lights where the app puts them (state.window.trafficLightFrames):
// all three on one line from (12, 12), evenly apart.
export function trafficLightsInPlace(lights: number[][]): boolean {
	const [close, miniaturize, zoom] = lights;
	const spacing = (miniaturize?.[0] ?? 0) - (close?.[0] ?? 0);

	return (
		close?.[0] === 12 &&
		lights.every((frame) => frame[1] === 12) &&
		spacing > 0 &&
		(zoom?.[0] ?? 0) - (miniaturize?.[0] ?? 0) === spacing
	);
}

export type PaneLaunch = {
	// Reuse a data directory (for relaunch checks); a throwaway one otherwise,
	// removed with the scenario's other directories when it ends.
	dataDir?: string;
	args?: string[];
	env?: Record<string, string>;
	// Another copy of the app than the run's binary.
	binary?: string;
	// The data root the app derives from `env`'s HOME and CFFIXED_USER_HOME
	// (a throwaway folder), as on a Mac: the move of an earlier identifier's
	// data runs only then (ADR 0114). `dataDir` names where it lands; the
	// automation channel and the home go beside it, so nothing is in it
	// before the launch.
	derivedDataRoot?: boolean;
	// The volume is meant not to attach (data-volume): the launch returns
	// once it has failed, where it fails otherwise, saying why.
	volumeFails?: boolean;
	// Where the process starts, as a terminal's folder would be.
	cwd?: string;
};

export async function launchPane(
	context: ScenarioContext,
	options: PaneLaunch = {},
): Promise<PaneApp> {
	const dataDir = options.dataDir ?? context.tempDir("pane-data-");

	const own = options.derivedDataRoot
		? context.tempDir("pane-automation-")
		: dataDir;

	const automationDir = join(own, "automation");
	rmSync(automationDir, { recursive: true, force: true });
	mkdirSync(join(automationDir, "in"), { recursive: true });
	mkdirSync(join(automationDir, "out"), { recursive: true });

	// macOS reports the window occluded while the screen is locked, and in
	// the background on an unlocked one, the user's windows over it: Chromium
	// then hides its pages (no visual updates, no hang monitor, a chooser
	// ended, an external app's prompt never asked). Chromium's own switch for
	// this keeps pages visible; in front the window is the user's to see.
	const occluded = !context.foreground || (await isScreenLocked());

	// A home of its own, kept with the data so a relaunch finds it (see
	// browser.ts's launch).
	const home = join(own, "home");
	mkdirSync(home, { recursive: true });

	const browser = await context.launch({
		binary: options.binary,
		userDataDir: join(dataDir, "Engine"),
		derivedDataRoot: options.derivedDataRoot,
		cwd: options.cwd,
		args: [
			...(occluded ? ["--disable-backgrounding-occluded-windows"] : []),
			...(options.args ?? []),
		],
		env: {
			HOME: home,
			CFFIXED_USER_HOME: home,
			...(!options.derivedDataRoot && { PANE_DATA_DIR: dataDir }),
			PANE_AUTOMATION_DIR: automationDir,
			// Only then does the app take the front from the app in use.
			...(context.foreground && { PANE_AUTOMATION_FOREGROUND: "1" }),
			...(context.appearance && {
				PANE_SYSTEM_APPEARANCE: context.appearance,
			}),
			// Every launch checks for updates: against the fixtures, never
			// GitHub.
			PANE_UPDATE_URL: context.fixtures.url("releases/latest"),
			// A first launch migrates the Electron app's data: never the
			// user's, which lives at the default path. The migration scenario
			// names its own.
			PANE_ELECTRON_DATA_DIR: join(dataDir, "no-electron-app"),
			// A new tab opens the fixtures' page, not Google's: nothing leaves
			// this Mac but what a scenario opens itself.
			PANE_NEW_TAB_URL: context.fixtures.url("smoke.html?new-tab"),
			...options.env,
		},
	});

	const app = new PaneApp(browser, dataDir, automationDir, context.out);
	await app.waitFor("the window", (state) => state.window.visible);

	// The data key opens the profiles (ADR 0113): at once without a PIN, and
	// after an earlier Pane's data moves into the volume. Until then nothing
	// runs, and a profile's page waits for the volume to attach, so the app is
	// handed over once the profiles are open and the volume attached, as a
	// moment after launch, or the PIN screen shows. benchmark-unlock times
	// what the attach adds. A volume that would not attach ends the wait.
	const launched = await app.waitFor(
		"the profiles open and the volume attached, or the PIN screen",
		(state) =>
			(state.security.data.phase === "open" && state.security.data.attached) ||
			state.security.locked ||
			state.security.data.failure !== null,
		60_000,
	);

	if (launched.security.data.failure !== null && !options.volumeFails) {
		await browser.close();

		throw new Error(
			`the volume did not attach: ${launched.security.data.failure}`,
		);
	}

	return app;
}

// Sets up a PIN through the PIN screen, as Settings does, once the key has opened.
export async function setUpPin(app: PaneApp, pin: string) {
	await app.send("showPinScreen", { mode: "setup" });
	await app.send("pinKeys", { keys: `${pin}>` });

	await app.waitFor(
		"the PIN asked again",
		(state) => state.security.screen?.step === "confirm",
		10_000,
	);

	await app.send("pinKeys", { keys: pin });

	await app.waitFor(
		"the PIN set up",
		(state) =>
			state.security.screen === null && state.security.pinLength === pin.length,
		10_000,
	);
}

// A key the window takes (Esc, Return), as the user's key reaches it: with
// the front (run.ts --foreground), posted to the app's event queue once its
// window is key; without it, through Views' own dispatch to what has the
// window's keys (dispatchKey), so the app in use keeps the front.
export async function pressWindowKey(
	context: ScenarioContext,
	app: PaneApp,
	key: string,
): Promise<void> {
	if (!context.foreground) {
		await app.send("dispatchKey", { key });

		return;
	}

	// A key the window takes needs it key, as when the user is in Pane;
	// another app may have taken it during the run.
	await app.send("activate");
	await app.waitFor("the key window", (state) => state.window.key, 10_000);
	await app.send("key", { key });
}

// Brings the app in front with its window key, for a check that needs it
// (keys a panel takes, a page's fullscreen): true once it is. Without
// --foreground the app in use keeps the front, and the check is deferred.
export async function bringToFront(
	context: ScenarioContext,
	app: PaneApp,
	check: string,
): Promise<boolean> {
	await app.send("activate");

	const key = await app
		.waitFor("the key window", (state) => state.window.key, 5_000)
		.then(
			() => true,
			() => false,
		);

	if (!key && context.foreground) {
		throw new Error(`the window never became key for ${check}`);
	}

	if (!key) {
		context.defer(`${check}: it needs Pane in front (run.ts --foreground)`);
	}

	return key;
}
