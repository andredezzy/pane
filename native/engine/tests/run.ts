// Scenario runner. Launches a Chromium-based binary (stock Chromium or Pane),
// serves the fixtures in pages/, runs each scenario, and writes one JSON file
// per scenario plus report.json into the output directory.
//
// Usage: bun native/engine/tests/run.ts --binary <path> --out <dir> <scenario>...
import { mkdirSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { parseArgs } from "node:util";
import { type Browser, type LaunchOptions, launch } from "./lib/browser";
import { type Fixtures, serveFixtures } from "./lib/fixtures";

export type ScenarioContext = {
	binary: string;
	fixtures: Fixtures;
	out: string;
	launch: (options?: LaunchOptions) => Promise<Browser>;
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
	},
});

if (!values.binary || !values.out || positionals.length === 0) {
	console.error("usage: run.ts --binary <path> --out <dir> <scenario>...");
	process.exit(2);
}

const binary = resolve(values.binary);
const out = resolve(values.out);
mkdirSync(out, { recursive: true });

const fixtures = serveFixtures();
const results: Record<string, unknown>[] = [];

for (const name of positionals) {
	const browsers: Browser[] = [];
	const startedAt = Date.now();
	let entry: Record<string, unknown>;

	try {
		const scenario: Scenario = (await import(`./scenarios/${name}.ts`)).default;

		const result = await scenario({
			binary,
			fixtures,
			out,
			launch: async (options) => {
				const browser = await launch(binary, options);
				browsers.push(browser);

				return browser;
			},
		});

		entry = { name, ok: true, ms: Date.now() - startedAt, result };
	} catch (error) {
		entry = {
			name,
			ok: false,
			ms: Date.now() - startedAt,
			error: String(error),
		};
	} finally {
		await Promise.all(browsers.map((browser) => browser.close()));
	}

	writeFileSync(
		join(out, `${name}.json`),
		`${JSON.stringify(entry, null, "\t")}\n`,
	);

	results.push(entry);

	console.log(
		`${entry.ok ? "PASS" : "FAIL"} ${name} (${entry.ms} ms)${entry.ok ? "" : ` — ${entry.error}`}`,
	);
}

fixtures.stop();

const report = {
	binary,
	finishedAt: new Date().toISOString(),
	passed: results.every((r) => r.ok),
	results,
};

writeFileSync(
	join(out, "report.json"),
	`${JSON.stringify(report, null, "\t")}\n`,
);

process.exit(report.passed ? 0 : 1);
