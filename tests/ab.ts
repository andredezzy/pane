// A/B runner: two builds of Pane take turns at each benchmark, round after
// round, so a trend in the Mac's load falls on both, and the order flips each
// round, so neither always runs second. Each run is the scenario runner's,
// into <out>/r<round>/<base|candidate>, its output in a .log beside the
// result. compare.json and compare.md then give, for every median and
// percentile in a benchmark's result, the median across rounds for each
// build, and the difference. A failed run shows in the table, and the rest
// go on. Both are written again after each run, so a stopped pass leaves
// what it measured.
//
// Usage: bun tests/ab.ts --base <Pane binary> --candidate <Pane binary>
//        --out <dir> [--rounds 3] [benchmark...]
// A benchmark is a name in benchmarks/ (launch for benchmark-launch.ts).
// interaction-timing runs only when named: it takes the front.
import { existsSync, mkdirSync, rmSync, writeFileSync } from "node:fs";
import { basename, join, resolve } from "node:path";
import { parseArgs } from "node:util";
import { stats } from "./lib/benchmark";

const DEFAULT = ["launch", "unlock", "memory", "stores", "pages", "scroll"];
const SIDES = ["base", "candidate"] as const;

type Side = (typeof SIDES)[number];

type Run = {
	round: number;
	benchmark: string;
	side: Side;
	ok: boolean;
	ms?: number;
	error?: string;
	result?: unknown;
};

// The leaves compared: medians and percentiles, as the benchmarks name them
// (median and p95 from benchmark.ts, medianMs and p90Ms).
const COMPARED = /^(median|p\d+)(Ms)?$/;

const { values, positionals } = parseArgs({
	args: Bun.argv.slice(2),
	allowPositionals: true,
	options: {
		base: { type: "string" },
		candidate: { type: "string" },
		out: { type: "string" },
		rounds: { type: "string", default: "3" },
	},
});

const rounds = Number(values.rounds);
const names = positionals.length > 0 ? positionals : DEFAULT;

const scenarioOf = (name: string) =>
	[`benchmark-${name}.ts`, `${name}.ts`]
		.map((file) => join(import.meta.dir, "benchmarks", file))
		.find((path) => existsSync(path));

const unknown = names.filter((name) => !scenarioOf(name));

if (
	!values.base ||
	!values.candidate ||
	!values.out ||
	!Number.isInteger(rounds) ||
	rounds < 1 ||
	unknown.length > 0
) {
	console.error(
		`usage: ab.ts --base <binary> --candidate <binary> --out <dir> [--rounds <n>] [benchmark...]${unknown.length > 0 ? `\nno benchmark named ${unknown.join(", ")}` : ""}`,
	);

	process.exit(2);
}

const out = resolve(values.out);

const binaries: Record<Side, string> = {
	base: resolve(values.base),
	candidate: resolve(values.candidate),
};

for (const binary of Object.values(binaries)) {
	if (!existsSync(binary)) {
		console.error(`no binary at ${binary}`);
		process.exit(2);
	}
}

// Every compared leaf of a result, by its path. Arrays hold the runs
// themselves, which the leaves summarize.
function leaves(
	value: unknown,
	path: string[] = [],
	into = new Map<string, number>(),
): Map<string, number> {
	if (value === null || typeof value !== "object" || Array.isArray(value)) {
		return into;
	}

	for (const [key, child] of Object.entries(value)) {
		if (typeof child === "number" && COMPARED.test(key)) {
			into.set([...path, key].join("."), child);
		} else {
			leaves(child, [...path, key], into);
		}
	}

	return into;
}

const medianOf = (numbers: number[]) =>
	numbers.length > 0 ? stats(numbers).median : null;

function compare(runs: Run[]) {
	return names.map((benchmark) => {
		const results = Object.fromEntries(
			SIDES.map((side) => [
				side,
				runs
					.filter(
						(run) => run.benchmark === benchmark && run.side === side && run.ok,
					)
					.map((run) => leaves(run.result)),
			]),
		) as Record<Side, Map<string, number>[]>;

		const paths = new Set(
			SIDES.flatMap((side) =>
				results[side].flatMap((leaf) => [...leaf.keys()]),
			),
		);

		const rows = [...paths].map((path) => {
			const samples = (side: Side) =>
				results[side].flatMap((leaf) => leaf.get(path) ?? []);

			const base = medianOf(samples("base"));
			const candidate = medianOf(samples("candidate"));

			const difference =
				base === null || candidate === null ? null : candidate - base;

			return {
				path,
				base,
				candidate,
				difference,
				percent:
					difference === null || !base ? null : (difference / base) * 100,
				rounds: {
					base: samples("base").length,
					candidate: samples("candidate").length,
				},
			};
		});

		const failures = runs
			.filter((run) => run.benchmark === benchmark && !run.ok)
			.map(({ round, side, error }) => ({ round, side, error }));

		return { benchmark, rows, failures };
	});
}

// Bytes in MB; the rest as measured (ms, counts).
function format(path: string, value: number | null, signed = false): string {
	if (value === null) {
		return "—";
	}

	const bytes = path.includes("Bytes");
	const shown = (bytes ? value / 1e6 : value).toFixed(1);

	return `${signed && value > 0 ? "+" : ""}${shown}${bytes ? " MB" : ""}`;
}

function write(runs: Run[]) {
	const benchmarks = compare(runs);

	const report = {
		binaries,
		rounds,
		runs: runs.map(({ result: _result, ...run }) => run),
		benchmarks,
	};

	writeFileSync(
		join(out, "compare.json"),
		`${JSON.stringify(report, null, "\t")}\n`,
	);

	const lines = [
		"# Base against candidate",
		"",
		`Base \`${binaries.base}\`, candidate \`${binaries.candidate}\`, ${rounds} round${rounds === 1 ? "" : "s"}, taking turns. A value is the median of the rounds' values; n, the rounds each build gave.`,
	];

	for (const { benchmark, rows, failures } of benchmarks) {
		lines.push("", `## ${benchmark}`, "");

		for (const { round, side, error } of failures) {
			lines.push(`- Round ${round}, ${side}, failed: ${error}`);
		}

		if (failures.length > 0) {
			lines.push("");
		}

		if (rows.length === 0) {
			lines.push("No results.");

			continue;
		}

		lines.push(
			"| Leaf | Base | Candidate | Difference | % | n |",
			"|---|---:|---:|---:|---:|---:|",
		);

		for (const row of rows) {
			const percent =
				row.percent === null
					? "—"
					: `${row.percent > 0 ? "+" : ""}${row.percent.toFixed(1)}%`;

			lines.push(
				`| ${row.path} | ${format(row.path, row.base)} | ${format(row.path, row.candidate)} | ${format(row.path, row.difference, true)} | ${percent} | ${row.rounds.base}/${row.rounds.candidate} |`,
			);
		}
	}

	writeFileSync(join(out, "compare.md"), `${lines.join("\n")}\n`);
}

const runs: Run[] = [];

for (let round = 1; round <= rounds; round++) {
	for (const benchmark of names) {
		const scenario = scenarioOf(benchmark) ?? "";
		const name = basename(scenario, ".ts");

		for (const side of round % 2 === 1 ? SIDES : [...SIDES].reverse()) {
			const dir = join(out, `r${round}`, side);
			const resultPath = join(dir, `${name}.json`);
			mkdirSync(dir, { recursive: true });
			rmSync(resultPath, { force: true });

			const ran =
				await Bun.$`${process.execPath} ${join(import.meta.dir, "run.ts")} --binary ${binaries[side]} --out ${dir} --timeout 2400 ${scenario}`
					.nothrow()
					.quiet();

			writeFileSync(join(dir, `${name}.log`), `${ran.stdout}${ran.stderr}`);

			const entry: Omit<Run, "round" | "benchmark" | "side"> = existsSync(
				resultPath,
			)
				? await Bun.file(resultPath).json()
				: {
						ok: false,
						error: `no result (exit code ${ran.exitCode}; see ${name}.log)`,
					};

			runs.push({
				round,
				benchmark,
				side,
				ok: entry.ok,
				ms: entry.ms,
				error: entry.error,
				result: entry.result,
			});

			console.log(
				`r${round} ${benchmark} ${side}: ${entry.ok ? "PASS" : `FAIL — ${entry.error}`}`,
			);

			write(runs);
		}
	}
}

process.exit(runs.every((run) => run.ok) ? 0 : 1);
