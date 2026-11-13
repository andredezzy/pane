import { mkdtempSync, readFileSync, realpathSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import type { Subprocess } from "bun";
import { Cdp } from "./cdp";
import { bySystem, noWindowsSide } from "./platform";
import { detachImagesIn } from "./tools";

// The environment that gives the app the throwaway folder `home` for its own:
// on the Mac, HOME, and CFFIXED_USER_HOME for Foundation, so its caches, HTTP
// storage, Downloads, Application Support, and Crashpad's database are not
// the user's Pane's (its preferences stay the user's domain: cfprefsd ignores
// a fixed home). Chromium on Windows reads its folders (AppData, Downloads)
// from the shell, which no variable moves.
export const homeEnvironment = bySystem({
	mac: (home: string): Record<string, string> => ({
		HOME: home,
		CFFIXED_USER_HOME: home,
	}),
	windows: (_home: string): Record<string, string> => {
		throw noWindowsSide(
			"a home of the app's own",
			"Chromium reads Windows' folders from the shell, not the environment",
		);
	},
});

// The app the executable `binary` runs: its bundle on the Mac (Pane.app, from
// Pane.app/Contents/MacOS/Pane); chrome.exe's folder on Windows, which holds
// the version's folder and the rest of the app beside it.
export const appOf = bySystem({
	mac: (binary: string) => {
		const bundle = binary.replace(/\/Contents\/MacOS\/[^/]+$/, "");

		if (bundle === binary) {
			throw new Error(`${binary} is not in an app bundle`);
		}

		return bundle;
	},
	windows: (binary: string) => dirname(binary),
});

// The app's resource `name` (crl-set): in the bundle's Resources on the Mac.
// Where Windows' app keeps its own, the CRLSet among them, is W7's.
export const appResource = bySystem({
	mac: (binary: string, name: string) =>
		join(appOf(binary), "Contents", "Resources", name),
	windows: (_binary: string, _name: string): string => {
		throw noWindowsSide(
			"the app's resources",
			"W7 decides where Windows' app keeps them",
		);
	},
});

export type LaunchOptions = {
	args?: string[];
	env?: Record<string, string>;
	// Chromium's user data folder, where DevToolsActivePort is written: a
	// throwaway one unless a launch names it (launchPane's is in its data).
	userDataDir?: string;
	// Another copy than the run's binary: a scenario that replaces the app's
	// bundle (update-install) runs one of its own.
	binary?: string;
	// The Pane app derives its data root from its home, as on a Mac, rather
	// than PANE_DATA_DIR: the move ADR 0114 makes at launch runs only then.
	// Only under a home of the scenario's own, fixed for Foundation too
	// (CFFIXED_USER_HOME): with HOME alone, the app's Application Support is
	// the user's.
	derivedDataRoot?: boolean;
	// Where the process starts, as a terminal's folder would be.
	cwd?: string;
};

export type Browser = {
	// How the process ended, once it has.
	exit: () => { code: number | null; signal: string | null };
	pid: number;
	port: number;
	cdp: Cdp;
	// Quits the browser, or kills it after 10 s. Safe to call more than once.
	close: () => Promise<void>;
};

// The DevTools port once the browser serves it. Fails at once when the
// browser exits first, rather than after the whole minute.
async function waitForPort(
	child: Subprocess,
	userDataDir: string,
): Promise<number> {
	const deadline = Date.now() + 60_000;

	while (Date.now() < deadline) {
		if (child.exitCode !== null || child.signalCode !== null) {
			throw new Error(
				`the browser exited before it served DevTools (${child.signalCode ?? `exit code ${child.exitCode}`})`,
			);
		}

		let port = 0;

		try {
			port = Number(
				readFileSync(join(userDataDir, "DevToolsActivePort"), "utf8").split(
					"\n",
				)[0],
			);
		} catch {}

		// The file can be there before its port is written: an empty
		// line reads as 0.
		if (Number.isInteger(port) && port > 0) {
			return port;
		}

		await Bun.sleep(250);
	}

	throw new Error("browser never exposed its DevTools port");
}

// Launches a Chromium-based binary with remote debugging and connects to it.
export async function launch(
	binary: string,
	options: LaunchOptions = {},
): Promise<Browser> {
	const throwaway = options.userDataDir === undefined;

	const userDataDir =
		options.userDataDir ?? mkdtempSync(join(tmpdir(), "pane-profile-"));

	// The Pane app keeps its own data (PANE_DATA_DIR) and migrates the
	// Electron app's on a first launch: unless a scenario names them, both
	// are throwaway, never the user's at their default paths.
	if (options.derivedDataRoot) {
		const fixed = options.env?.CFFIXED_USER_HOME;

		if (
			!fixed ||
			options.env?.HOME !== fixed ||
			!realpathSync(fixed).startsWith(realpathSync(tmpdir()))
		) {
			throw new Error(
				"a derived data root needs HOME and CFFIXED_USER_HOME on the same throwaway folder",
			);
		}
	}

	const appDataDir =
		options.env?.PANE_DATA_DIR || options.derivedDataRoot
			? null
			: mkdtempSync(join(tmpdir(), "pane-data-"));

	// A home of its own too, unless a scenario names one: under the real
	// home the app would share the user's Pane's caches, HTTP storage, and
	// Downloads, and Chromium's Crashpad database (homeEnvironment).
	const home = options.env?.HOME
		? null
		: mkdtempSync(join(tmpdir(), "pane-home-"));

	// A data volume still attached inside (ADR 0113), as one a killed app's
	// watchdog has yet to detach, goes first: removing its mount point fails.
	const removeDirs = async () => {
		if (throwaway) {
			rmSync(userDataDir, { recursive: true, force: true });
		}

		for (const directory of [appDataDir, home]) {
			if (directory) {
				await detachImagesIn(directory);
				rmSync(directory, { recursive: true, force: true });
			}
		}
	};

	// What gives the app that home; a launch that can't give it one (Windows,
	// for now) leaves nothing behind.
	let homeVariables: Record<string, string> = {};

	try {
		homeVariables = home ? homeEnvironment(home) : {};
	} catch (error) {
		await removeDirs();

		throw error;
	}

	// A reused directory still holds the last run's port.
	rmSync(join(userDataDir, "DevToolsActivePort"), { force: true });

	const args = [
		"--remote-debugging-port=0",
		"--no-first-run",
		"--no-default-browser-check",
		// Scenarios never touch the user's login keychain (Chromium's
		// "Safe Storage" item would prompt for the password on every launch
		// of a freshly signed build).
		"--use-mock-keychain",
		`--user-data-dir=${userDataDir}`,
		...(options.args ?? []),
	];

	// PANE_SCENARIO_STDERR=1 shows the binary's log, for debugging a scenario.
	const output = process.env.PANE_SCENARIO_STDERR ? "inherit" : "ignore";

	const child = Bun.spawn([binary, ...args], {
		cwd: options.cwd,
		env: {
			...process.env,
			...(appDataDir && {
				PANE_DATA_DIR: appDataDir,
				PANE_ELECTRON_DATA_DIR: join(appDataDir, "no-electron-app"),
			}),
			...homeVariables,
			...options.env,
		},
		stdout: output,
		stderr: output,
	});

	const exited = (timeoutMs: number) =>
		Promise.race([
			child.exited.then(() => true),
			Bun.sleep(timeoutMs).then(() => false),
		]);

	let port: number;
	let cdp: Cdp;

	// A launch that fails leaves nothing behind: no app, no directories.
	try {
		port = await waitForPort(child, userDataDir);

		const version = (await (
			await fetch(`http://127.0.0.1:${port}/json/version`, {
				signal: AbortSignal.timeout(10_000),
			})
		).json()) as {
			webSocketDebuggerUrl: string;
		};

		cdp = await Cdp.connect(version.webSocketDebuggerUrl);
	} catch (error) {
		child.kill("SIGKILL");
		await exited(5_000);
		await removeDirs();

		throw error;
	}

	let closing: Promise<void> | null = null;

	const close = async () => {
		// An app that already quit (Quit Pane, the panic wipe) has no one to
		// answer Browser.close.
		if (child.exitCode === null && child.signalCode === null) {
			await cdp.send("Browser.close", {}, undefined, 5_000).catch(() => {});
		}

		cdp.close();

		if (!(await exited(10_000))) {
			child.kill("SIGKILL");
			await exited(5_000);
		}

		await removeDirs();
	};

	return {
		pid: child.pid,
		port,
		exit: () => ({
			code: child.exitCode,
			signal: child.signalCode === null ? null : String(child.signalCode),
		}),
		cdp,
		close: () => {
			closing ??= close();

			return closing;
		},
	};
}
