// Scenario runner. Launches a Chromium-based binary (stock Chromium or Pane),
// serves the fixtures in pages/, runs each scenario, and writes one JSON file
// per scenario plus report.json into the output directory.
//
// Usage: bun tests/run.ts --binary <path> --out <dir>
//        [--timeout <seconds per scenario, 600>] <scenario>...
import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { parseArgs } from "node:util";
import { type Browser, type LaunchOptions, launch } from "./lib/browser";
import { isScreenLocked } from "./lib/cua";
import { type Fixtures, serveFixtures } from "./lib/fixtures";
import { killUsing } from "./lib/processes";
import { swiftTool } from "./lib/tools";

export type ScenarioContext = {
	binary: string;
	fixtures: Fixtures;
	out: string;
	launch: (options?: LaunchOptions) => Promise<Browser>;
	// A new directory in $TMPDIR for the scenario alone. It is removed when
	// the scenario ends, pass or fail, after every process still using it
	// is stopped.
	tempDir: (prefix: string) => string;
	// Records a check the scenario skipped (a locked screen hides what it
	// checks), so the report tells a full pass from a partial one.
	defer: (check: string) => void;
};

export type Scenario = (
	context: ScenarioContext,
) => Promise<Record<string, unknown>>;

export class ScenarioFailure extends Error {}

export function expect(condition: unknown, message: string): asserts condition {
	if (!condition) {
		throw new ScenarioFailure(message);
	}
}

const { values, positionals } = parseArgs({
	args: Bun.argv.slice(2),
	allowPositionals: true,
	options: {
		binary: { type: "string" },
		out: { type: "string" },
		timeout: { type: "string", default: "600" },
	},
});

const timeoutMs = Number(values.timeout) * 1000;

if (
	!values.binary ||
	!values.out ||
	positionals.length === 0 ||
	!(timeoutMs > 0)
) {
	console.error(
		"usage: run.ts --binary <path> --out <dir> [--timeout <seconds>] <scenario>...",
	);

	process.exit(2);
}

const binary = resolve(values.binary);
const out = resolve(values.out);
mkdirSync(out, { recursive: true });

// A promise a scenario left behind (one still running past its deadline)
// can fail after the runner moved on: report it, and run the rest.
process.on("unhandledRejection", (reason) => {
	console.error(`unhandled rejection: ${String(reason)}`);
});

const fixtures = serveFixtures();
const results: Record<string, unknown>[] = [];

// Quits what the scenario launched, stops whatever still uses its
// directories (an app that started itself again), and removes them.
async function tearDown(browsers: Browser[], tempDirs: string[]) {
	await Promise.all(browsers.map((browser) => browser.close()));

	for (const directory of tempDirs) {
		await killUsing(directory);
		rmSync(directory, { recursive: true, force: true });
	}
}

// Caps Lock on draws macOS's indicator by every focused field, which the
// window checks read as a stray window: say so before anything runs.
if ((await Bun.$`${await swiftTool("caps-lock")}`.text()).trim() === "on") {
	console.error(
		"warning: Caps Lock is on; macOS's indicator by focused fields fails the window checks",
	);
}

for (const name of positionals) {
	const browsers: Browser[] = [];
	const tempDirs: string[] = [];
	const deferred: string[] = [];
	// Past its deadline a scenario may still be running: it launches nothing
	// more, and makes no more directories.
	let ended = false;
	const lockedAtStart = await isScreenLocked();
	const startedAt = Date.now();
	let deadline: ReturnType<typeof setTimeout> | undefined;
	let entry: Record<string, unknown>;

	const context: ScenarioContext = {
		binary,
		fixtures,
		out,
		launch: async (options) => {
			if (ended) {
				throw new Error("the scenario has ended");
			}

			const browser = await launch(binary, options);
			browsers.push(browser);

			if (ended) {
				await browser.close();

				throw new Error("the scenario has ended");
			}

			return browser;
		},
		tempDir: (prefix) => {
			if (ended) {
				throw new Error("the scenario has ended");
			}

			const directory = mkdtempSync(join(tmpdir(), prefix));
			tempDirs.push(directory);

			return directory;
		},
		defer: (check) => {
			deferred.push(check);
		},
	};

	try {
		const scenario: Scenario = (await import(`./scenarios/${name}.ts`)).default;
		const running = scenario(context);
		// Raced below; it can still fail after its deadline.
		running.catch(() => {});

		const result = await Promise.race([
			running,
			new Promise<never>((_, reject) => {
				deadline = setTimeout(
					() =>
						reject(
							new ScenarioFailure(
								`timed out after ${timeoutMs / 1000} s (--timeout)`,
							),
						),
					timeoutMs,
				);
			}),
		]);

		entry = { name, ok: true, ms: Date.now() - startedAt, result };
	} catch (error) {
		entry = {
			name,
			ok: false,
			ms: Date.now() - startedAt,
			error: String(error),
			...(error instanceof ScenarioFailure || !(error instanceof Error)
				? {}
				: { stack: error.stack }),
		};
	} finally {
		clearTimeout(deadline);
		ended = true;
	}

	const tearingDown = Date.now();
	await tearDown(browsers, tempDirs);

	// Checks only an unlocked screen allows were skipped, or ran another way.
	const locked = lockedAtStart || (await isScreenLocked());

	entry = {
		...entry,
		teardownMs: Date.now() - tearingDown,
		locked,
		deferred,
	};

	writeFileSync(
		join(out, `${name}.json`),
		`${JSON.stringify(entry, null, "\t")}\n`,
	);

	results.push(entry);

	const notes = [
		...(locked ? ["locked screen"] : []),
		...(deferred.length > 0 ? [`deferred: ${deferred.join("; ")}`] : []),
	];

	console.log(
		`${entry.ok ? "PASS" : "FAIL"} ${name} (${entry.ms} ms${notes.length > 0 ? `; ${notes.join("; ")}` : ""})${entry.ok ? "" : ` — ${entry.error}`}`,
	);
}

fixtures.stop();

const report = {
	binary,
	finishedAt: new Date().toISOString(),
	passed: results.every((r) => r.ok),
	// Every check ran: nothing was deferred for a locked screen.
	complete: results.every((r) => r.ok && (r.deferred as string[]).length === 0),
	results,
};

writeFileSync(
	join(out, "report.json"),
	`${JSON.stringify(report, null, "\t")}\n`,
);

process.exit(report.passed ? 0 : 1);
