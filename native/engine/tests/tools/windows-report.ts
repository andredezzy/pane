// Builds windows-per-scenario.json from a run's scenario results: for each
// scenario, the windows sampled while it ran, by what they were to the app.
// Usage: bun windows-report.ts <run dir> [expected-extra=<scenario>:<class>...]
import { readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";

type Other = { at: number; class: string; title: string; frame: string };

const [dir, ...rules] = process.argv.slice(2);

if (!dir) {
	console.error("usage: windows-report.ts <run dir> [scenario:WindowClass...]");
	process.exit(2);
}

// Windows a scenario opens by design (the picture-in-picture player).
const expected = new Map(
	rules.map((rule) => {
		const [scenario, windowClass] = rule.split(":");

		return [scenario, windowClass];
	}),
);

const report = JSON.parse(readFileSync(join(dir, "report.json"), "utf8")) as {
	results: { name: string; ok: boolean }[];
};

const scenarios = report.results.map(({ name, ok }) => {
	const entry = JSON.parse(readFileSync(join(dir, `${name}.json`), "utf8")) as {
		result?: {
			windows?: {
				samples: number;
				maxCgWindows: number;
				roles: string[];
				others: Other[];
			};
		};
	};

	const windows = entry.result?.windows;
	const others = windows?.others ?? [];
	const allowed = expected.get(name);
	const unexpected = others.filter((window) => window.class !== allowed);

	return {
		scenario: name,
		passed: ok,
		samples: windows?.samples ?? 0,
		roles: windows?.roles ?? [],
		maxCgWindows: windows?.maxCgWindows ?? null,
		expectedExtraWindow: allowed ?? null,
		expectedExtraSeen: others.some((window) => window.class === allowed),
		chromeWindows: unexpected,
	};
});

const out = {
	generatedAt: new Date().toISOString(),
	rule: "Every visible window must be the app's own: its window, a sheet on it, an app-modal panel, or its open menu. Anything else is Chrome's UI.",
	clean: scenarios.every(
		(scenario) => scenario.samples > 0 && scenario.chromeWindows.length === 0,
	),
	scenarios,
};

writeFileSync(
	join(dir, "windows-per-scenario.json"),
	`${JSON.stringify(out, null, "\t")}\n`,
);

console.log(`${scenarios.length} scenarios, clean: ${out.clean}`);
