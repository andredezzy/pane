// Launches the Pane app with its data in a throwaway (or given) directory and
// drives it through the automation channel (PANE_AUTOMATION_DIR, see
// app/Pane/Automation/Automation.swift).
import {
	existsSync,
	mkdirSync,
	readFileSync,
	renameSync,
	rmSync,
	writeFileSync,
} from "node:fs";
import { join } from "node:path";
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

export type ProfileState = {
	id: string;
	name: string;
	color: string;
	expanded: boolean;
	// The proxy's last connection test, for a proxied profile.
	proxyStatus: "testing" | "connected" | "failed" | null;
	// What the engine applied, once the profile is open.
	network: { proxyRules: string | null; webRTCIPHandlingPolicy: string } | null;
	keepLoaded: boolean;
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

export type DownloadState = {
	id: string;
	url: string;
	path: string | null;
	receivedBytes: number;
	totalBytes: number;
	state: "inProgress" | "complete" | "cancelled" | "interrupted";
};

export type SecurityState = {
	pinLength: number | null;
	failedAttempts: number;
	locked: boolean;
	// The panic wipe's erase, while it runs, and what it could not erase.
	wipe: { erasing: boolean; failures: string[] };
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

export type UpdateState = {
	status: "idle" | "checking" | "available" | "downloading" | "downloaded";
	available: string | null;
	progress: number | null;
	eta: number | null;
	pill: boolean;
	checked: boolean;
	// The downloaded update, staged beside the app to install at quit (ADR 0092).
	installsItself: boolean;
};

// An item of a page's right-click menu; a submenu lists its own.
export type MenuItemState = {
	title: string;
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
	page: "browser" | "settings" | "extensions";
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
		trafficLights: number[];
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
	}[];
	pageFullscreen: boolean;
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
	printRequests: number;
	extensions: Record<string, ExtensionState[]>;
	extensionPopup: {
		extension: string;
		open: boolean;
		width: number;
		height: number;
	} | null;
};

export type ExtensionState = {
	id: string;
	name: string;
	version: string;
	enabled: boolean;
	pinned: boolean;
	hasAction: boolean;
	hasPopup: boolean;
	actionTitle: string;
	badgeText: string;
	hasIcon: boolean;
	// The icon draws something (it can be there and blank).
	iconVisible: boolean;
};

type Reply = { ok: boolean; result?: unknown; error?: string; state: AppState };

let nextCommand = 0;

export class PaneApp {
	constructor(
		readonly browser: Browser,
		readonly dataDir: string,
		private readonly automationDir: string,
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

		const deadline = Date.now() + 15_000;

		while (Date.now() < deadline) {
			if (existsSync(reply)) {
				const parsed = JSON.parse(readFileSync(reply, "utf8")) as Reply;
				rmSync(reply, { force: true });

				if (!parsed.ok) {
					throw new Error(`${command}: ${parsed.error}`);
				}

				return { result: parsed.result as T, state: parsed.state };
			}

			await Bun.sleep(50);
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
};

export async function launchPane(
	context: ScenarioContext,
	options: PaneLaunch = {},
): Promise<PaneApp> {
	const dataDir = options.dataDir ?? context.tempDir("pane-data-");
	const automationDir = join(dataDir, "automation");
	rmSync(automationDir, { recursive: true, force: true });
	mkdirSync(join(automationDir, "in"), { recursive: true });
	mkdirSync(join(automationDir, "out"), { recursive: true });

	// macOS reports the window occluded while the screen is locked, and
	// Chromium then hides its pages: no visual updates, no hang monitor.
	// Chromium's own switch for this keeps pages visible.
	const locked = await isScreenLocked();

	const browser = await context.launch({
		binary: options.binary,
		userDataDir: join(dataDir, "Engine"),
		args: [
			...(locked ? ["--disable-backgrounding-occluded-windows"] : []),
			...(options.args ?? []),
		],
		env: {
			PANE_DATA_DIR: dataDir,
			PANE_AUTOMATION_DIR: automationDir,
			// Every launch checks for updates: against the fixtures, never
			// GitHub.
			PANE_UPDATE_URL: context.fixtures.url("releases/latest"),
			// A first launch migrates the Electron app's data: never the
			// user's, which lives at the default path. The migration scenario
			// names its own.
			PANE_ELECTRON_DATA_DIR: join(dataDir, "no-electron-app"),
			...options.env,
		},
	});

	const app = new PaneApp(browser, dataDir, automationDir, context.out);
	await app.waitFor("the window", (state) => state.window.visible);

	return app;
}
