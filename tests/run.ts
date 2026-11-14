// Scenario runner. Launches the Pane binary it is given, serves the fixtures
// in pages/, runs each scenario, and writes one JSON file per scenario plus
// report.json into the output directory.
//
// Usage: bun tests/run.ts --binary <path> --out <dir>
//        [--timeout <seconds per scenario, 600>] <scenario>...
// A scenario is a name in scenarios/, or the path of a .ts file of its form
// (benchmarks/, rigs/), which reports under its base name. A scenario runs on
// the Mac and Windows unless it says otherwise (lib/platform.ts's
// NotOnWindows): one kept off this system is reported, not run.
import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { basename, join, resolve } from "node:path";
import { parseArgs } from "node:util";
import { type Browser, type LaunchOptions, launch } from "./lib/browser";
import { isScreenLocked } from "./lib/computer-use";
import { type Fixtures, serveFixtures } from "./lib/fixtures";
import { keptOffThisSystem, type NotOnWindows } from "./lib/platform";
import { killUsing } from "./lib/processes";
import { capsLockIndicator, detachImagesIn, forgetBundle } from "./lib/tools";

export type ScenarioContext = {
	binary: string;
	fixtures: Fixtures;
	out: string;
	// Whether the app may take the front from the app in use (--foreground):
	// keys a panel takes, a page's fullscreen, Quit Pane?. Otherwise a
	// scenario defers what needs it.
	foreground: boolean;
	// The system appearance Pane's launches take (--appearance), in place of
	// the Mac's, which a scenario can't change on a Mac in use: renders of
	// either one, the Lock's included. Undefined leaves the Mac's.
	appearance: "light" | "dark" | undefined;
	launch: (options?: LaunchOptions) => Promise<Browser>;
	// A new directory in $TMPDIR for the scenario alone. It is removed when
	// the scenario ends, pass or fail, after every process still using it
	// is stopped.
	tempDir: (prefix: string) => string;
	// Records a check the scenario skipped (a locked screen hides what it
	// checks, or Pane may not take the front without --foreground), so the
	// report tells a full pass from a partial one.
	defer: (check: string) => void;
};

// What it returns goes into its entry of the report: a benchmark's numbers.
// A scenario that checks returns nothing.
export type Scenario = (context: ScenarioContext) => Promise<unknown>;

class ScenarioFailure extends Error {}

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
		foreground: { type: "boolean", default: false },
		appearance: { type: "string" },
	},
});

const timeoutMs = Number(values.timeout) * 1000;

if (
	!values.binary ||
	!values.out ||
	positionals.length === 0 ||
	!(timeoutMs > 0) ||
	(values.appearance !== undefined &&
		values.appearance !== "light" &&
		values.appearance !== "dark")
) {
	console.error(
		"usage: run.ts --binary <path> --out <dir> [--timeout <seconds>] [--foreground] [--appearance light|dark] <scenario>...",
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
		await detachImagesIn(directory);
		await killUsing(directory);
		rmSync(directory, { recursive: true, force: true });
	}
}

// Caps Lock on draws macOS's indicator by every focused field, which the
// window checks read as a stray window: say so before anything runs.
if (await capsLockIndicator()) {
	console.error(
		"warning: Caps Lock is on; macOS's indicator by focused fields fails the window checks",
	);
}

for (const given of positionals) {
	const name = basename(given, ".ts");
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
		foreground: values.foreground,
		appearance: values.appearance as ScenarioContext["appearance"],
		launch: async (options) => {
			if (ended) {
				throw new Error("the scenario has ended");
			}

			const browser = await launch(options?.binary ?? binary, options);
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
		const module = (await import(
			given.endsWith(".ts") ? resolve(given) : `./scenarios/${name}.ts`
		)) as { default: Scenario; notOnWindows?: NotOnWindows };

		const keptOff = keptOffThisSystem(module.notOnWindows);

		if (keptOff) {
			// Reported, not run: what Windows does its own way, with no design
			// yet, as checks not made; what is the Mac's alone, as nothing to
			// check there.
			if ("needsDesign" in keptOff) {
				context.defer(`all of it, on Windows: ${keptOff.needsDesign}`);
			}

			entry = {
				name,
				ok: true,
				ms: 0,
				notRun:
					"macOnly" in keptOff
						? `the Mac's alone: ${keptOff.macOnly}`
						: `Windows' own design to come: ${keptOff.needsDesign}`,
			};
		} else {
			const running = module.default(context);
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

			entry = {
				name,
				ok: true,
				ms: Date.now() - startedAt,
				...(result === undefined ? {} : { result }),
			};
		}
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

	if (entry.notRun) {
		console.log(`SKIP ${name} (${entry.notRun})`);
	} else {
		console.log(
			`${entry.ok ? "PASS" : "FAIL"} ${name} (${entry.ms} ms${notes.length > 0 ? `; ${notes.join("; ")}` : ""})${entry.ok ? "" : ` — ${entry.error}`}`,
		);
	}
}

fixtures.stop();
await forgetBundle(binary);

const report = {
	binary,
	finishedAt: new Date().toISOString(),
	passed: results.every((r) => r.ok),
	// Every check ran: nothing was deferred for a locked screen, or for the
	// front Pane may not take.
	complete: results.every((r) => r.ok && (r.deferred as string[]).length === 0),
	results,
};

writeFileSync(
	join(out, "report.json"),
	`${JSON.stringify(report, null, "\t")}\n`,
);

process.exit(report.passed ? 0 : 1);
