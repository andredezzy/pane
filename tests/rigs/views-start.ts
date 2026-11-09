import {
	existsSync,
	mkdirSync,
	readFileSync,
	realpathSync,
	statSync,
	writeFileSync,
} from "node:fs";
import { homedir } from "node:os";
import { dirname, join } from "node:path";
import { type AppState, launchPane, type PaneApp } from "../lib/pane";
import {
	imagesAttachedIn,
	mountedAt,
	pgrep,
	waitForExit,
} from "../lib/processes";
import { swiftTool } from "../lib/tools";
import { expect, type Scenario, type ScenarioContext } from "../run";

// The Views build's start (docs/plans/views-migration.md, C5): before the
// engine, what main.swift did, in its order, and the data's lock; once the
// thread pool runs, the vault and the lock, which without a PIN open the data
// key at once; and once the engine stops, the volume's detach. A launch on a
// fresh data folder makes the volume's image there, attaches it, and opens it,
// and its quit detaches it and lets go of the lock; the next launch attaches
// the image as it starts and opens it with the key the first one sealed. A
// data root derived from a home of the run's own takes an earlier
// identifier's folder and drops the session caches there, and nowhere else.
// A data folder outside the temporary folder is refused, as any launch is
// until the Lock's UI exists. With a watchdog beside the binary (a packaged
// bundle's), it runs while Pane does, goes at the quit, and after a kill
// detaches the image itself. For the Views build:
//
//   bun tests/run.ts --binary <Pane.app>/Contents/MacOS/Pane --out <dir> \
//     tests/rigs/views-start.ts

// The data's lock, by flock(2), as Pane takes it: "held", "free", or "missing".
async function dataLock(dataDir: string): Promise<string> {
	return (
		await Bun.$`${await swiftTool("data-lock")} ${dataDir}`.text()
	).trim();
}

function json(path: string): unknown {
	return JSON.parse(readFileSync(path, "utf8"));
}

// A key sealed in security.json and its copy, and no PIN (null).
function sealedWithoutPin(dataDir: string): boolean {
	return ["security.json", "Data.security.json"].every((name) => {
		const { state } = json(join(dataDir, name)) as {
			state: { key?: unknown; pin?: unknown };
		};

		return state.key !== undefined && (state.pin ?? null) === null;
	});
}

// Chromium's Local State names Default as used last, with no profiles open
// last, as Pane leaves it at a quit.
function startsOnDefault(dataDir: string): boolean {
	const state = json(join(dataDir, "Engine", "Local State")) as {
		profile?: { last_used?: string; last_active_profiles?: string[] };
		tab_stats?: unknown;
	};

	return (
		state.profile?.last_used === "Default" &&
		(state.profile.last_active_profiles ?? []).length === 0 &&
		state.tab_stats === undefined
	);
}

// The bundle carries the volume's watchdog beside the loader (package.sh);
// out/Release's has none, and a launch goes on without one.
function hasWatchdog(context: ScenarioContext): boolean {
	return existsSync(join(dirname(context.binary), "pane-volume-watch"));
}

// What the vault reports, and what is on the disk and in the process table,
// while Pane runs on `dataDir`.
async function whileOpen(
	context: ScenarioContext,
	app: PaneApp,
	dataDir: string,
): Promise<Record<string, unknown>> {
	const state: AppState = await app.state();
	const { data } = state.security;
	const watchdogs = await pgrep("-f", `pane-volume-watch ${app.browser.pid} `);

	const open = {
		phase: data.phase,
		attached: data.attached,
		failure: data.failure,
		locked: state.security.locked,
		openedInMs: data.attachedAt
			? Math.round(data.attachedAt.ended - data.attachedAt.started)
			: null,
		image: existsSync(join(dataDir, "Data.sparsebundle")),
		attachedImages: (await imagesAttachedIn(dataDir)).length,
		mounted: await mountedAt(join(dataDir, "Engine", "Volume")),
		sealedWithoutPin: sealedWithoutPin(dataDir),
		dataMode: (statSync(dataDir).mode & 0o777).toString(8),
		lock: await dataLock(dataDir),
		watchdogs: watchdogs.length,
	};

	const watchdog = hasWatchdog(context);

	expect(
		open.phase === "open" &&
			open.attached &&
			open.failure === null &&
			!open.locked &&
			open.openedInMs !== null &&
			open.image &&
			open.attachedImages === 1 &&
			open.mounted &&
			open.sealedWithoutPin &&
			open.dataMode === "700" &&
			open.lock === "held" &&
			open.watchdogs === (watchdog ? 1 : 0),
		`the data open: ${JSON.stringify(open)}`,
	);

	return open;
}

// The quit, from the command to the process gone: the image detached before,
// and the lock let go of with it.
async function quit(
	app: PaneApp,
	dataDir: string,
): Promise<Record<string, unknown>> {
	const pid = app.browser.pid;
	const started = Date.now();
	await app.quit();

	expect(await waitForExit(pid, 30_000), "Pane did not quit");

	const after = {
		quitInMs: Date.now() - started,
		attachedImages: (await imagesAttachedIn(dataDir)).length,
		mounted: await mountedAt(join(dataDir, "Engine", "Volume")),
		lock: await dataLock(dataDir),
		watchdogs: (await pgrep("-f", `pane-volume-watch ${pid} `)).length,
		startsOnDefault: startsOnDefault(dataDir),
	};

	await app.browser.close();

	expect(
		after.attachedImages === 0 &&
			!after.mounted &&
			after.lock === "free" &&
			after.watchdogs === 0 &&
			after.startsOnDefault,
		`after the quit: ${JSON.stringify(after)}`,
	);

	return after;
}

const viewsStart: Scenario = async (context) => {
	// Refused: a data folder outside the temporary folder, which a launch can't
	// write either, so a wrong guard writes nothing.
	const outside = "/var/empty/pane-views-start";

	const refused = await context
		.launch({
			env: {
				PANE_DATA_DIR: outside,
				PANE_ELECTRON_DATA_DIR: join(outside, "no-electron-app"),
			},
		})
		.then(
			async (browser) => {
				await browser.close();

				return "launched";
			},
			(error: unknown) => String(error),
		);

	expect(
		refused.includes("exit code 1") && !existsSync(outside),
		`a data folder outside the temporary folder: ${refused}`,
	);

	// A fresh data folder: the volume made, attached, and opened; the quit
	// detaches it and lets go of the lock.
	const dataDir = realpathSync(context.tempDir("pane-views-start-"));
	const first = await launchPane(context, { dataDir });
	const firstOpen = await whileOpen(context, first, dataDir);
	const firstQuit = await quit(first, dataDir);

	// Again on the same data: the image attaches as Pane starts, and the key
	// the first launch sealed opens it.
	const second = await launchPane(context, { dataDir });
	const secondOpen = await whileOpen(context, second, dataDir);
	const secondQuit = await quit(second, dataDir);

	// Killed, Pane detaches nothing: its watchdog does, once the process is
	// gone, and lets go of the data's lock after.
	let killed: Record<string, unknown> | string =
		"no watchdog beside the binary";

	if (hasWatchdog(context)) {
		const killedApp = await launchPane(context, { dataDir });
		await whileOpen(context, killedApp, dataDir);
		const pid = killedApp.browser.pid;
		process.kill(pid, "SIGKILL");

		expect(await waitForExit(pid, 10_000), "Pane outlived its kill");

		const started = Date.now();

		while (
			(await imagesAttachedIn(dataDir)).length > 0 &&
			Date.now() - started < 10_000
		) {
			await Bun.sleep(50);
		}

		killed = {
			detachedInMs: Date.now() - started,
			attachedImages: (await imagesAttachedIn(dataDir)).length,
			lock: await dataLock(dataDir),
			watchdogs: (await pgrep("-f", `pane-volume-watch ${pid} `)).length,
		};

		await killedApp.browser.close();

		expect(
			killed.attachedImages === 0 &&
				killed.lock === "free" &&
				killed.watchdogs === 0,
			`after a kill: ${JSON.stringify(killed)}`,
		);
	} else {
		context.defer(
			"the watchdog's detach after a kill: no watchdog beside this binary (package.sh puts it in the bundle)",
		);
	}

	// A data root derived from a home of the run's own (ADR 0114): an earlier
	// identifier's folder moves to this one's, and an earlier Pane's session
	// caches go, in that home. The user's own folders are checked untouched.
	const user = join(homedir(), "Library");

	const userFolders = () =>
		[
			["Application Support", "com.pane.desktop"],
			["Application Support", "xyz.roundedcorners.pane"],
			["Caches", "com.pane.desktop"],
			["Caches", "xyz.roundedcorners.pane"],
			["HTTPStorages", "com.pane.desktop"],
			["HTTPStorages", "xyz.roundedcorners.pane"],
		].map((path) => existsSync(join(user, ...path)));

	const userBefore = userFolders();
	const home = realpathSync(context.tempDir("pane-views-start-home-"));
	const library = join(home, "Library");
	const support = join(library, "Application Support");
	const former = join(support, "com.pane.desktop");
	const derived = join(support, "xyz.roundedcorners.pane");
	const storages = join(library, "HTTPStorages");
	const sessionCaches = join(storages, "xyz.roundedcorners.pane");

	mkdirSync(former, { recursive: true });
	writeFileSync(join(former, "moved"), "an earlier Pane's");
	mkdirSync(sessionCaches, { recursive: true });

	const derivedApp = await launchPane(context, {
		dataDir: derived,
		derivedDataRoot: true,
		env: { HOME: home, CFFIXED_USER_HOME: home },
	});

	const derivedOpen = await whileOpen(context, derivedApp, derived);

	const moved = {
		formerGone: !existsSync(former),
		movedIn: existsSync(join(derived, "moved")),
		sessionCachesGone: !existsSync(sessionCaches),
		cachesMode: (
			statSync(join(library, "Caches", "xyz.roundedcorners.pane")).mode & 0o777
		).toString(8),
	};

	const derivedQuit = await quit(derivedApp, derived);

	const userUntouched =
		JSON.stringify(userFolders()) === JSON.stringify(userBefore);

	expect(
		moved.formerGone &&
			moved.movedIn &&
			moved.sessionCachesGone &&
			moved.cachesMode === "700",
		`the derived data root: ${JSON.stringify(moved)}`,
	);

	expect(
		userUntouched,
		`the run reached the user's own folders: ${JSON.stringify(userBefore)}`,
	);

	return {
		refused,
		first: { open: firstOpen, quit: firstQuit },
		second: { open: secondOpen, quit: secondQuit },
		killed,
		derived: {
			open: derivedOpen,
			moved,
			quit: derivedQuit,
			userUntouched,
		},
	};
};

export default viewsStart;
