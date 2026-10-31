import { mkdtempSync, readFileSync, realpathSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import type { Subprocess } from "bun";
import { Cdp } from "./cdp";
import { detachImagesIn } from "./processes";

export type LaunchOptions = {
	args?: string[];
	env?: Record<string, string>;
	// Stock Chromium gets a throwaway profile; the Pane app manages its own data.
	userDataDir?: string | null;
	// Another copy than the run's binary: a scenario that replaces the app's
	// bundle (update-install) runs one of its own.
	binary?: string;
	// The Pane app derives its data root from its home, as on a Mac, rather
	// than PANE_DATA_DIR: the move ADR 0114 makes at launch runs only then.
	// Only under a home of the scenario's own, fixed for Foundation too
	// (CFFIXED_USER_HOME): with HOME alone, the app's Application Support is
	// the user's.
	derivedDataRoot?: boolean;
};

export type Browser = {
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
	userDataDir: string | null,
	fixedPort: number | null,
): Promise<number> {
	const deadline = Date.now() + 60_000;

	while (Date.now() < deadline) {
		if (child.exitCode !== null || child.signalCode !== null) {
			throw new Error(
				`the browser exited before it served DevTools (${child.signalCode ?? `exit code ${child.exitCode}`})`,
			);
		}

		if (fixedPort !== null) {
			const response = await fetch(
				`http://127.0.0.1:${fixedPort}/json/version`,
				{ signal: AbortSignal.timeout(2_000) },
			).catch(() => null);

			if (response?.ok) {
				return fixedPort;
			}
		} else if (userDataDir) {
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
		}

		await Bun.sleep(250);
	}

	throw new Error("browser never exposed its DevTools port");
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

// Launches a Chromium-based binary with remote debugging and connects to it.
export async function launch(
	binary: string,
	options: LaunchOptions = {},
): Promise<Browser> {
	const throwaway = options.userDataDir === undefined;

	const userDataDir = throwaway
		? mkdtempSync(join(tmpdir(), "pane-profile-"))
		: options.userDataDir;

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
	// Downloads, and Chromium's Crashpad database. Its preferences stay the
	// user's domain: cfprefsd ignores a fixed home (CFFIXED_USER_HOME).
	const home = options.env?.HOME
		? null
		: mkdtempSync(join(tmpdir(), "pane-home-"));

	// A data volume still attached inside (ADR 0113), as one a killed app's
	// watchdog has yet to detach, goes first: removing its mount point fails.
	const removeDirs = async () => {
		if (throwaway && userDataDir) {
			rmSync(userDataDir, { recursive: true, force: true });
		}

		for (const directory of [appDataDir, home]) {
			if (directory) {
				await detachImagesIn(directory);
				rmSync(directory, { recursive: true, force: true });
			}
		}
	};

	const fixedPort = userDataDir ? null : freePort();

	// A reused directory still holds the last run's port.
	if (userDataDir) {
		rmSync(join(userDataDir, "DevToolsActivePort"), { force: true });
	}

	const args = [
		`--remote-debugging-port=${fixedPort ?? 0}`,
		"--no-first-run",
		"--no-default-browser-check",
		// Scenarios never touch the user's login keychain (Chromium's
		// "Safe Storage" item would prompt for the password on every launch
		// of a freshly signed build).
		"--use-mock-keychain",
		...(userDataDir ? [`--user-data-dir=${userDataDir}`] : []),
		...(options.args ?? []),
	];

	// PANE_SCENARIO_STDERR=1 shows the binary's log, for debugging a scenario.
	const output = process.env.PANE_SCENARIO_STDERR ? "inherit" : "ignore";

	const child = Bun.spawn([binary, ...args], {
		env: {
			...process.env,
			...(appDataDir && {
				PANE_DATA_DIR: appDataDir,
				PANE_ELECTRON_DATA_DIR: join(appDataDir, "no-electron-app"),
			}),
			...(home && { HOME: home, CFFIXED_USER_HOME: home }),
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
		port = await waitForPort(child, userDataDir ?? null, fixedPort);

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
		cdp,
		close: () => {
			closing ??= close();

			return closing;
		},
	};
}
