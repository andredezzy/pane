import { mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { Cdp } from "./cdp";

export type LaunchOptions = {
	args?: string[];
	env?: Record<string, string>;
	// Stock Chromium gets a throwaway profile; the Pane app manages its own data.
	userDataDir?: string | null;
};

export type Browser = {
	pid: number;
	port: number;
	cdp: Cdp;
	close: () => Promise<void>;
};

async function waitForPort(
	userDataDir: string | null,
	fixedPort: number | null,
): Promise<number> {
	const deadline = Date.now() + 60_000;

	while (Date.now() < deadline) {
		if (fixedPort !== null) {
			const response = await fetch(
				`http://127.0.0.1:${fixedPort}/json/version`,
			).catch(() => null);

			if (response?.ok) {
				return fixedPort;
			}
		} else if (userDataDir) {
			try {
				return Number(
					readFileSync(join(userDataDir, "DevToolsActivePort"), "utf8").split(
						"\n",
					)[0],
				);
			} catch {}
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
	const appDataDir = options.env?.PANE_DATA_DIR
		? null
		: mkdtempSync(join(tmpdir(), "pane-data-"));

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
			...options.env,
		},
		stdout: output,
		stderr: output,
	});

	const port = await waitForPort(userDataDir ?? null, fixedPort);

	const version = (await (
		await fetch(`http://127.0.0.1:${port}/json/version`)
	).json()) as {
		webSocketDebuggerUrl: string;
	};

	const cdp = await Cdp.connect(version.webSocketDebuggerUrl);

	return {
		pid: child.pid,
		port,
		cdp,
		close: async () => {
			await cdp.send("Browser.close").catch(() => {});
			cdp.close();

			const exited = await Promise.race([
				child.exited.then(() => true),
				Bun.sleep(10_000).then(() => false),
			]);

			if (!exited) {
				child.kill("SIGKILL");
			}

			if (throwaway && userDataDir) {
				rmSync(userDataDir, { recursive: true, force: true });
			}

			if (appDataDir) {
				rmSync(appDataDir, { recursive: true, force: true });
			}
		},
	};
}
