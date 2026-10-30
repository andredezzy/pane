// The benchmark of the Electron app against the native one (the benchmark-*
// scenarios, docs/benchmarks/README.md). Both apps launch the same way, on
// throwaway data, with the same flags, and are measured with the same tools:
// the window list for the window, the pages' own Performance API for loads,
// footprint for memory, and the kernel's counters for CPU.
import {
	mkdirSync,
	readFileSync,
	realpathSync,
	rmSync,
	writeFileSync,
} from "node:fs";
import { loadavg } from "node:os";
import { join } from "node:path";
import type { Subprocess } from "bun";
import type { ScenarioContext } from "../run";
import { Cdp, openPage, type Page } from "./cdp";
import { PaneApp } from "./pane";
import { waitForExit } from "./processes";
import { targets } from "./targets";
import { swiftTool } from "./tools";

export type AppKind = "electron" | "native";

export type BenchmarkedApp = {
	kind: AppKind;
	executable: string;
	bundle: string;
	version: string;
	// The bundle as given and as its helpers name it (/tmp is /private/tmp):
	// a process of the app has its executable inside one.
	paths: string[];
};

// The Electron app's one profile, seeded before its first launch: the
// native app makes its own, "Personal", when it opens its launch URL.
const ELECTRON_PROFILE = "6f9d2c1a-8b3e-4c7d-9a2f-1e5b7c3d9f01";
const PROFILE_NAME = "Personal";

// What both apps are given. The Mac is in use while the benchmark runs, and
// another app's window can cover the benchmark's: Chromium would throttle an
// occluded window or a background renderer. The Electron app checks GitHub
// for updates at launch, with no switch to turn it off, so its host does not
// resolve; the native app's check goes to the fixtures.
const SHARED_ARGS = [
	"--no-first-run",
	"--no-default-browser-check",
	"--use-mock-keychain",
	"--disable-backgrounding-occluded-windows",
	"--disable-renderer-backgrounding",
	"--host-resolver-rules=MAP api.github.com ~NOTFOUND",
];

// Milliseconds since 1970, to the microsecond: the clock the window tool and
// the pages' performance.timeOrigin read.
export const now = () => performance.timeOrigin + performance.now();

async function describe(
	kind: AppKind,
	executable: string,
): Promise<BenchmarkedApp> {
	const end = executable.indexOf(".app/");

	if (end < 0) {
		throw new Error(`${executable} is not in an app bundle`);
	}

	const bundle = executable.slice(0, end + 4);

	const version = (
		await Bun.$`plutil -extract CFBundleShortVersionString raw -o - ${join(bundle, "Contents/Info.plist")}`.text()
	).trim();

	return {
		kind,
		executable,
		bundle,
		version,
		paths: [...new Set([bundle, realpathSync(bundle)])],
	};
}

// The Electron app, from PANE_BENCHMARK_ELECTRON, and the run's binary, the
// native app.
export async function benchmarkedApps(
	context: ScenarioContext,
): Promise<BenchmarkedApp[]> {
	const electron = process.env.PANE_BENCHMARK_ELECTRON;

	if (!electron) {
		throw new Error(
			"PANE_BENCHMARK_ELECTRON names no Electron app (docs/benchmarks/README.md)",
		);
	}

	// Compiled now, not inside the first timed launch.
	await swiftTool("window-wait");

	return [
		await describe("electron", electron),
		await describe("native", context.binary),
	];
}

// The apps in a run's order: they take turns going first, so a trend in the
// Mac's load falls on both.
export function inTurn<T>(apps: T[], run: number): T[] {
	return run % 2 === 0 ? apps : [...apps].reverse();
}

export type LaunchTimes = {
	// Milliseconds from the exec. The window: the first on-screen window,
	// and how long before it the window list last showed none.
	window: number | null;
	windowResolution: number | null;
	devTools: number;
	// The Electron app's UI, a page of its own: its first paint, its load,
	// and its sidebar listing the profile (shownAtCheck: it already did when
	// first checked, so the time is an upper bound).
	ui: {
		firstContentfulPaint: number | null;
		loaded: number;
		profileShown: number;
		shownAtCheck: boolean;
	} | null;
	// When the Electron app was asked for the tab (the native app opens its
	// launch URL itself).
	tabRequested: number | null;
	// The first tab's page.
	page: { firstContentfulPaint: number | null; loaded: number };
};

type PageTiming = {
	timeOrigin: number;
	loadEventEnd: number;
	firstContentfulPaint: number | null;
};

const PAGE_TIMING = `(() => {
	const navigation = performance.getEntriesByType("navigation")[0];
	const paint = performance.getEntriesByName("first-contentful-paint")[0];
	return {
		timeOrigin: performance.timeOrigin,
		loadEventEnd: navigation ? navigation.loadEventEnd : 0,
		firstContentfulPaint: paint ? paint.startTime : null,
	};
})()`;

// A page's timing once its load event has ended and it has painted. A small
// page loads before its first paint is recorded; one that never paints (it
// stays hidden) has none after 3 s.
async function loadedTiming(
	page: Page,
	timeoutMs = 60_000,
): Promise<PageTiming> {
	const deadline = Date.now() + timeoutMs;
	let loadedAt: number | null = null;

	while (Date.now() < deadline) {
		const timing = await page
			.evaluate<PageTiming>(PAGE_TIMING)
			.catch(() => null);

		if (timing && timing.loadEventEnd > 0) {
			loadedAt ??= Date.now();

			if (
				timing.firstContentfulPaint !== null ||
				Date.now() - loadedAt > 3_000
			) {
				return timing;
			}
		}

		await Bun.sleep(10);
	}

	throw new Error("the page never finished loading");
}

// The page target at `url`, once it has committed, attached.
async function pageAt(
	cdp: Cdp,
	url: string,
	timeoutMs = 60_000,
): Promise<Page> {
	const deadline = Date.now() + timeoutMs;

	while (Date.now() < deadline) {
		const target = (await targets(cdp)).find(
			(info) => info.type === "page" && info.url === url,
		);

		if (target) {
			const page = await openPage(cdp, "about:blank", target.targetId);

			while (Date.now() < deadline) {
				const href = await page
					.evaluate<string>("location.href")
					.catch(() => "");

				if (href === url) {
					return page;
				}

				await Bun.sleep(10);
			}
		}

		await Bun.sleep(10);
	}

	throw new Error(`no page at ${url}`);
}

let nextRequest = 900_000_000;

// A mutation of the Electron app's tRPC router, sent from its UI the way
// its own client sends one. Ids start high, clear of the client's own.
// Returns when it was sent, by the UI's clock.
async function electronMutation(
	ui: Page,
	path: string,
	input: unknown,
): Promise<number> {
	const id = nextRequest++;

	return ui.evaluate<number>(`new Promise((resolve, reject) => {
		const timer = setTimeout(() => reject(new Error("no reply to ${path}")), 15000);
		let sentAt = 0;
		window.electronTRPC.onMessage((message) => {
			if (message.id !== ${id}) {
				return;
			}
			clearTimeout(timer);
			if (message.error) {
				reject(new Error(JSON.stringify(message.error)));
			} else {
				resolve(sentAt);
			}
		});
		sentAt = performance.timeOrigin + performance.now();
		window.electronTRPC.sendMessage({
			method: "request",
			operation: { id: ${id}, type: "mutation", path: ${JSON.stringify(path)}, input: ${JSON.stringify(input)}, context: {} },
		});
	})`);
}

// When the Electron app's sidebar first lists the profile, by the UI's clock.
const PROFILE_SHOWN = `new Promise((resolve) => {
	const shown = () => (document.body?.textContent ?? "").includes(${JSON.stringify(PROFILE_NAME)});
	const at = () => performance.timeOrigin + performance.now();
	if (shown()) {
		resolve({ at: at(), atCheck: true });
		return;
	}
	const observer = new MutationObserver(() => {
		if (shown()) {
			observer.disconnect();
			resolve({ at: at(), atCheck: false });
		}
	});
	observer.observe(document, { childList: true, subtree: true, characterData: true });
})`;

export type AppProcess = {
	pid: number;
	type: string;
	rssBytes: number;
};

function processType(command: string): string {
	const type = /--type=(\S+)/.exec(command)?.[1];
	const subType = /--utility-sub-type=(\S+)/.exec(command)?.[1];

	if (type) {
		return subType ? `${type} (${subType})` : type;
	}

	return command.includes("crashpad") ? "crashpad" : "browser";
}

// Every running process of the app, by its executable, inside the bundle:
// the browser, its helpers, and those that left its tree (Crashpad's handler
// is launchd's child). Never by its arguments, which the runner's own
// command line shares.
export async function processesOf(app: BenchmarkedApp): Promise<AppProcess[]> {
	const commands = new Map(
		(await Bun.$`ps -axww -o pid=,command=`.quiet().text())
			.split("\n")
			.flatMap((line) => {
				const match = /^\s*(\d+)\s+(.*)$/.exec(line);

				return match ? [[Number(match[1]), match[2] ?? ""] as const] : [];
			}),
	);

	const lines = (await Bun.$`ps -axww -o pid=,rss=,comm=`.quiet().text()).split(
		"\n",
	);

	return lines.flatMap((line) => {
		const match = /^\s*(\d+)\s+(\d+)\s+(.*)$/.exec(line);
		const executable = match?.[3] ?? "";

		if (
			!match ||
			!app.paths.some((path) => executable.startsWith(`${path}/`))
		) {
			return [];
		}

		const pid = Number(match[1]);

		return [
			{
				pid,
				type: processType(commands.get(pid) ?? executable),
				rssBytes: Number(match[2]) * 1024,
			},
		];
	});
}

export type MemorySnapshot = {
	processes: number;
	// Each process's physical footprint (what Activity Monitor's Memory
	// column shows), summed; footprint's own total for the set, which counts
	// memory shared between them once; and resident memory, summed.
	footprintSumBytes: number;
	footprintTotalBytes: number;
	rssSumBytes: number;
	byType: Record<string, { count: number; footprintBytes: number }>;
};

type FootprintJSON = {
	"total footprint": number;
	processes: { pid: number; footprint: number }[];
	errors: unknown[];
};

// The app's memory, read with footprint. A process that ends between the
// list and the read fails the read: it is read again.
export async function memoryOf(
	app: BenchmarkedApp,
	scratch: string,
): Promise<MemorySnapshot> {
	for (let attempt = 0; attempt < 5; attempt++) {
		const processes = await processesOf(app);
		const file = join(scratch, `footprint-${Date.now()}.json`);

		await Bun.$`footprint --json ${file} ${processes.flatMap((entry) => ["-p", String(entry.pid)])}`
			.quiet()
			.nothrow();

		const read = JSON.parse(readFileSync(file, "utf8")) as FootprintJSON;

		if (read.errors.length > 0 || read.processes.length !== processes.length) {
			await Bun.sleep(500);

			continue;
		}

		const footprints = new Map(
			read.processes.map((entry) => [entry.pid, entry.footprint]),
		);

		const byType: MemorySnapshot["byType"] = {};

		for (const entry of processes) {
			const group = byType[entry.type] ?? { count: 0, footprintBytes: 0 };
			group.count += 1;
			group.footprintBytes += footprints.get(entry.pid) ?? 0;
			byType[entry.type] = group;
		}

		return {
			processes: processes.length,
			footprintSumBytes: [...footprints.values()].reduce((a, b) => a + b, 0),
			footprintTotalBytes: read["total footprint"],
			rssSumBytes: processes.reduce(
				(total, entry) => total + entry.rssBytes,
				0,
			),
			byType,
		};
	}

	throw new Error(`footprint could not read the ${app.kind} app`);
}

export type Stats = {
	median: number;
	min: number;
	max: number;
	runs: number[];
};

export function stats(values: number[]): Stats {
	const sorted = [...values].sort((a, b) => a - b);
	const middle = Math.floor(sorted.length / 2);

	const median =
		sorted.length % 2 === 1
			? (sorted[middle] ?? Number.NaN)
			: ((sorted[middle - 1] ?? Number.NaN) + (sorted[middle] ?? Number.NaN)) /
				2;

	return {
		median,
		min: sorted[0] ?? Number.NaN,
		max: sorted.at(-1) ?? Number.NaN,
		runs: values,
	};
}

// The Mac the benchmark ran on.
export async function machine(): Promise<Record<string, string | number>> {
	const sysctl = async (name: string) =>
		(await Bun.$`sysctl -n ${name}`.text()).trim();

	const swVers = async (flag: string) =>
		(await Bun.$`sw_vers ${flag}`.text()).trim();

	return {
		model: await sysctl("hw.model"),
		chip: await sysctl("machdep.cpu.brand_string"),
		cores: Number(await sysctl("hw.ncpu")),
		memoryBytes: Number(await sysctl("hw.memsize")),
		macOS: await swVers("-productVersion"),
		build: await swVers("-buildVersion"),
	};
}

// The moment and the Mac's load averages (1, 5, 15 minutes): the Mac is in
// use while the benchmark runs.
export function conditions(): { at: string; loadAverage: number[] } {
	return {
		at: new Date().toISOString(),
		loadAverage: loadavg().map((value) => Math.round(value * 100) / 100),
	};
}

function freePort(): number {
	const server = Bun.serve({ port: 0, fetch: () => new Response() });
	const { port } = server;
	server.stop(true);

	if (port === undefined) {
		throw new Error("no free port");
	}

	return port;
}

export class RunningApp {
	constructor(
		readonly app: BenchmarkedApp,
		readonly child: Subprocess,
		readonly cdp: Cdp,
		readonly dataDir: string,
		readonly times: LaunchTimes,
		// The first tab's page.
		readonly page: Page,
		// The Electron app's UI page, or the native app's automation channel.
		private readonly ui: Page | null,
		private readonly pane: PaneApp | null,
	) {}

	// Opens a tab at `url` the way the app's UI does, and returns its page
	// once loaded.
	async openTab(url: string): Promise<Page> {
		if (this.ui) {
			await electronMutation(this.ui, "tabs.open", {
				profileId: ELECTRON_PROFILE,
				url,
			});
		} else if (this.pane) {
			const profile = (await this.pane.state()).profiles[0]?.id;

			if (!profile) {
				throw new Error("the native app has no profile");
			}

			await this.pane.openTab(profile, url);
		} else {
			throw new Error("launched without its automation channel");
		}

		const page = await pageAt(this.cdp, url);
		await loadedTiming(page);

		return page;
	}

	// Quits as the runner does (Browser.close), and waits for every process
	// of the app to end, killing what is left after 15 s.
	async quit(): Promise<void> {
		await this.cdp.send("Browser.close", {}, undefined, 5_000).catch(() => {});
		this.cdp.close();

		const exited = await Promise.race([
			this.child.exited.then(() => true),
			Bun.sleep(15_000).then(() => false),
		]);

		if (!exited) {
			this.child.kill("SIGKILL");
		}

		const deadline = Date.now() + 15_000;
		let left = await processesOf(this.app);

		while (left.length > 0 && Date.now() < deadline) {
			await Bun.sleep(100);
			left = await processesOf(this.app);
		}

		for (const entry of left) {
			try {
				process.kill(entry.pid, "SIGKILL");
			} catch {}

			await waitForExit(entry.pid, 5_000);
		}
	}
}

export type LaunchOptions = {
	// The first tab's page: the native app's launch URL; the Electron app,
	// which takes none, opens it from its UI once the UI lists the profile.
	url: string;
	// A relaunch reuses the data; a launch makes its own otherwise.
	dataDir?: string;
	// The native app's automation channel, for opening more tabs. It polls
	// its directory ten times a second, so the CPU benchmark leaves it off.
	automation?: boolean;
};

// Launches the app, timing the window, the UI, and the first tab's page from
// the exec.
export async function launchApp(
	context: ScenarioContext,
	app: BenchmarkedApp,
	options: LaunchOptions,
): Promise<RunningApp> {
	const dataDir =
		options.dataDir ?? context.tempDir(`pane-benchmark-${app.kind}-`);

	const port = freePort();
	const args = [`--remote-debugging-port=${port}`, ...SHARED_ARGS];
	let automationDir: string | null = null;

	// Each app gets a home of its own, the one macOS gives a process that
	// sets CFFIXED_USER_HOME: the Electron app keeps its data there, and both
	// share the user's bundle ID, so URLSession's cache and cookies (the
	// native app's update check) and Crashpad's database would otherwise be
	// the user's.
	const home = join(dataDir, "home");
	mkdirSync(home, { recursive: true });
	const env: Record<string, string> = { HOME: home, CFFIXED_USER_HOME: home };

	if (app.kind === "electron") {
		// The Electron app's data, at @pane/veil, with one profile.
		const userData = join(
			home,
			"Library",
			"Application Support",
			"@pane",
			"veil",
		);

		mkdirSync(userData, { recursive: true });
		const profiles = join(userData, "profiles.json");

		if (!options.dataDir) {
			writeFileSync(
				profiles,
				JSON.stringify({
					state: {
						profiles: [
							{
								id: ELECTRON_PROFILE,
								name: PROFILE_NAME,
								color: "BLUE",
								group: null,
								fingerprint: null,
								proxy: null,
								keepLoaded: false,
								tabs: [],
								createdAt: "2026-10-29T00:00:00.000Z",
								updatedAt: "2026-10-29T00:00:00.000Z",
							},
						],
					},
					version: 0,
				}),
			);
		}

		args.push(`--user-data-dir=${userData}`);
	} else {
		env.PANE_DATA_DIR = dataDir;
		env.PANE_UPDATE_URL = context.fixtures.url("releases/latest");
		env.PANE_ELECTRON_DATA_DIR = join(dataDir, "no-electron-app");

		if (options.automation) {
			automationDir = join(dataDir, "automation");
			rmSync(automationDir, { recursive: true, force: true });
			mkdirSync(join(automationDir, "in"), { recursive: true });
			mkdirSync(join(automationDir, "out"), { recursive: true });
			env.PANE_AUTOMATION_DIR = automationDir;
		}

		args.push(`--user-data-dir=${join(dataDir, "Engine")}`, options.url);
	}

	const output = process.env.PANE_SCENARIO_STDERR ? "inherit" : "ignore";
	const windowWait = await swiftTool("window-wait");

	const spawnedAt = now();

	const child = Bun.spawn([app.executable, ...args], {
		cwd: dataDir,
		env: { ...process.env, ...env },
		stdout: output,
		stderr: output,
	});

	const watcher = Bun.spawn([windowWait, String(child.pid), "60000"], {
		stdout: "pipe",
	});

	const window = new Response(watcher.stdout)
		.text()
		.then(
			(text) =>
				JSON.parse(text) as { seenAt: number | null; notSeenAt: number | null },
		)
		.catch(() => ({ seenAt: null, notSeenAt: null }));

	let cdp: Cdp | null = null;

	try {
		const since = (at: number) => at - spawnedAt;
		const deadline = Date.now() + 60_000;
		let devToolsAt = 0;
		let socket = "";

		while (!socket) {
			if (child.exitCode !== null || Date.now() > deadline) {
				throw new Error(
					`the ${app.kind} app never served DevTools (${child.exitCode === null ? "still running after 60 s" : `exit code ${child.exitCode}, signal ${child.signalCode}`})`,
				);
			}

			const response = await fetch(`http://127.0.0.1:${port}/json/version`, {
				signal: AbortSignal.timeout(1_000),
			}).catch(() => null);

			if (response?.ok) {
				devToolsAt = now();

				socket = ((await response.json()) as { webSocketDebuggerUrl: string })
					.webSocketDebuggerUrl;
			} else {
				await Bun.sleep(10);
			}
		}

		cdp = await Cdp.connect(socket);
		let ui: Page | null = null;
		let uiTimes: LaunchTimes["ui"] = null;
		let tabRequested: number | null = null;

		if (app.kind === "electron") {
			let target = null;

			while (!target) {
				if (Date.now() > deadline) {
					throw new Error("the Electron app's UI never loaded");
				}

				target = (await targets(cdp)).find(
					(info) =>
						info.type === "page" && info.url.endsWith("/renderer/index.html"),
				);

				if (!target) {
					await Bun.sleep(10);
				}
			}

			ui = await openPage(cdp, "about:blank", target.targetId);

			const shown = await ui.evaluate<{ at: number; atCheck: boolean }>(
				PROFILE_SHOWN,
			);

			tabRequested = since(
				await electronMutation(ui, "tabs.open", {
					profileId: ELECTRON_PROFILE,
					url: options.url,
				}),
			);

			const timing = await loadedTiming(ui);

			uiTimes = {
				firstContentfulPaint:
					timing.firstContentfulPaint === null
						? null
						: since(timing.timeOrigin + timing.firstContentfulPaint),
				loaded: since(timing.timeOrigin + timing.loadEventEnd),
				profileShown: since(shown.at),
				shownAtCheck: shown.atCheck,
			};
		}

		const page = await pageAt(cdp, options.url);
		const timing = await loadedTiming(page);
		const seen = await window;

		const times: LaunchTimes = {
			window: seen.seenAt === null ? null : since(seen.seenAt),
			windowResolution:
				seen.seenAt === null || seen.notSeenAt === null
					? null
					: seen.seenAt - seen.notSeenAt,
			devTools: since(devToolsAt),
			ui: uiTimes,
			tabRequested,
			page: {
				firstContentfulPaint:
					timing.firstContentfulPaint === null
						? null
						: since(timing.timeOrigin + timing.firstContentfulPaint),
				loaded: since(timing.timeOrigin + timing.loadEventEnd),
			},
		};

		const pane = automationDir
			? new PaneApp(
					{ pid: child.pid, port, cdp, close: async () => {} },
					dataDir,
					automationDir,
					context.out,
				)
			: null;

		return new RunningApp(app, child, cdp, dataDir, times, page, ui, pane);
	} catch (error) {
		cdp?.close();
		watcher.kill();
		child.kill("SIGKILL");
		await child.exited;

		throw error;
	}
}
