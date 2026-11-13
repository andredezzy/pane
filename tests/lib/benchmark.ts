// Timing Pane on its own, as the benchmarks do (tests/benchmarks,
// docs/benchmarks): the window list for the window, the pages' own Performance
// API for loads, and footprint for memory. On throwaway data, with one set of
// flags, so two builds compare.
import { mkdirSync, realpathSync, rmSync } from "node:fs";
import { loadavg } from "node:os";
import { join, sep } from "node:path";
import type { Subprocess } from "bun";
import type { ScenarioContext } from "../run";
import { appOf, homeEnvironment } from "./browser";
import { Cdp, openPage, type Page } from "./cdp";
import { PaneApp } from "./pane";
import { footprintsOf, processTable, waitForExit } from "./processes";
import { targets } from "./targets";
import { swiftTool } from "./tools";

export type BenchmarkedApp = {
	executable: string;
	// The app's folder (appOf) as given and as its helpers name it (/tmp is
	// /private/tmp): a process of the app has its executable inside one.
	paths: string[];
};

// The Mac is in use while a benchmark runs, and another app's window can cover
// the benchmark's: Chromium would throttle an occluded window or a background
// renderer.
const SHARED_ARGS = [
	"--no-first-run",
	"--no-default-browser-check",
	"--use-mock-keychain",
	"--disable-backgrounding-occluded-windows",
	"--disable-renderer-backgrounding",
];

// Milliseconds since 1970, to the microsecond: the clock the window tool and
// the pages' performance.timeOrigin read.
export const now = () => performance.timeOrigin + performance.now();

export async function benchmarkedApp(
	context: ScenarioContext,
): Promise<BenchmarkedApp> {
	const executable = context.binary;
	const folder = appOf(executable);

	// Compiled now, not inside the first timed launch.
	await swiftTool("window-wait");

	return { executable, paths: [...new Set([folder, realpathSync(folder)])] };
}

export type LaunchTimes = {
	// Milliseconds from the exec. The window: the first on-screen window,
	// and how long before it the window list last showed none.
	window: number | null;
	windowResolution: number | null;
	devTools: number;
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

type AppProcess = {
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

// Every running process of the app, by its executable, inside the app
// (appOf): the browser, its helpers, and those that left its tree (Crashpad's
// handler is launchd's child). Never by its arguments, which the runner's own
// command line shares.
async function processesOf(app: BenchmarkedApp): Promise<AppProcess[]> {
	return (await processTable()).flatMap((row) =>
		app.paths.some((path) => row.executable.startsWith(`${path}${sep}`))
			? [
					{
						pid: row.pid,
						type: processType(row.command),
						rssBytes: row.rssBytes,
					},
				]
			: [],
	);
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

// The app's memory, read with footprint. A process that ends between the
// list and the read fails the read: it is read again.
export async function memoryOf(
	app: BenchmarkedApp,
	scratch: string,
): Promise<MemorySnapshot> {
	for (let attempt = 0; attempt < 5; attempt++) {
		const processes = await processesOf(app);

		const read = await footprintsOf(
			processes.map((entry) => entry.pid),
			join(scratch, `footprint-${Date.now()}.json`),
		);

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

	throw new Error("footprint could not read the app");
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
		// Its automation channel, when asked for.
		private readonly pane: PaneApp | null,
	) {}

	// Opens a tab at `url` through the automation channel, and returns its
	// page once loaded.
	async openTab(url: string): Promise<Page> {
		const profile = (await this.pane?.state())?.profiles[0]?.id;

		if (!this.pane || !profile) {
			throw new Error("no automation channel, or no profile");
		}

		await this.pane.openTab(profile, url);
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
	// The first tab's page: the app opens it as its launch URL.
	url: string;
	// A relaunch reuses the data; a launch makes its own otherwise.
	dataDir?: string;
	// The automation channel, for opening more tabs. It polls its directory
	// ten times a second, so a launch being timed leaves it off.
	automation?: boolean;
};

// Launches the app, timing the window and the first tab's page from the exec.
export async function launchApp(
	context: ScenarioContext,
	app: BenchmarkedApp,
	options: LaunchOptions,
): Promise<RunningApp> {
	const dataDir = options.dataDir ?? context.tempDir("pane-benchmark-");
	const port = freePort();
	let automationDir: string | null = null;

	// A home of its own (homeEnvironment): URLSession's cache and cookies and
	// Crashpad's database would otherwise be the user's Pane's.
	const home = join(dataDir, "home");
	mkdirSync(home, { recursive: true });

	const env: Record<string, string> = {
		...homeEnvironment(home),
		PANE_DATA_DIR: dataDir,
		PANE_UPDATE_URL: context.fixtures.url("releases/latest"),
		PANE_ELECTRON_DATA_DIR: join(dataDir, "no-electron-app"),
	};

	if (options.automation) {
		automationDir = join(dataDir, "automation");
		rmSync(automationDir, { recursive: true, force: true });
		mkdirSync(join(automationDir, "in"), { recursive: true });
		mkdirSync(join(automationDir, "out"), { recursive: true });
		env.PANE_AUTOMATION_DIR = automationDir;
	}

	const args = [
		`--remote-debugging-port=${port}`,
		...SHARED_ARGS,
		`--user-data-dir=${join(dataDir, "Engine")}`,
		options.url,
	];

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
					`the app never served DevTools (${child.exitCode === null ? "still running after 60 s" : `exit code ${child.exitCode}, signal ${child.signalCode}`})`,
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
					{
						pid: child.pid,
						port,
						cdp,
						close: async () => {},
						exit: () => ({ code: child.exitCode, signal: null }),
					},
					dataDir,
					automationDir,
					context.out,
				)
			: null;

		return new RunningApp(app, child, cdp, dataDir, times, page, pane);
	} catch (error) {
		cdp?.close();
		watcher.kill();
		child.kill("SIGKILL");
		await child.exited;

		throw error;
	}
}
