// Gathers a benchmark run (the benchmark-* scenarios' results) into one JSON
// file, with the dmgs the apps came from, and prints the summary table of
// docs/benchmarks/native-vs-electron.md: the native app first, the Electron
// app as its baseline, each median with its runs' range in brackets, and the
// native app's change.
// Usage: bun benchmark-report.ts <run dir> <out json> [<native dmg> <electron dmg>]
import { createHash } from "node:crypto";
import { existsSync, readFileSync, statSync, writeFileSync } from "node:fs";
import { basename, join } from "node:path";

const [dir, out, nativeDmg, electronDmg] = process.argv.slice(2);

if (!dir || !out) {
	console.error(
		"usage: benchmark-report.ts <run dir> <out json> [<native dmg> <electron dmg>]",
	);

	process.exit(2);
}

const SCENARIOS = ["bundle", "launch", "memory", "cpu", "pages", "suites"];

type Stats = { median: number; min: number; max: number; runs: number[] };

// The scenarios' results, read by path.
type Tree = { [key: string]: Tree } & Partial<Stats>;

const results: Record<string, Tree> = {};

for (const name of SCENARIOS) {
	const file = join(dir, `benchmark-${name}.json`);

	if (existsSync(file)) {
		results[name] = JSON.parse(readFileSync(file, "utf8")).result;
	}
}

const download = (file: string | undefined) =>
	file
		? {
				file: basename(file),
				bytes: statSync(file).size,
				sha256: createHash("sha256").update(readFileSync(file)).digest("hex"),
			}
		: null;

const downloads = {
	native: download(nativeDmg),
	electron: download(electronDmg),
};

const at = (tree: Tree | undefined, path: string[]): Tree | undefined =>
	path.reduce<Tree | undefined>((node, key) => node?.[key], tree);

type Row = {
	metric: string;
	unit: string;
	better: "lower" | "higher";
	native: number | null;
	electron: number | null;
	// The median's runs' range, where there are runs.
	nativeRange?: [number, number];
	electronRange?: [number, number];
};

const MB = 2 ** 20;
const rows: Row[] = [];

function size(
	metric: string,
	native: number | undefined,
	electron: number | undefined,
) {
	rows.push({
		metric,
		unit: "MB",
		better: "lower",
		native: native === undefined ? null : native / MB,
		electron: electron === undefined ? null : electron / MB,
	});
}

// A median from each app's results, at `path` under the scenario.
function median(
	metric: string,
	unit: string,
	better: Row["better"],
	scenario: string,
	path: (kind: string) => string[],
	scale = 1,
) {
	const read = (kind: string) => at(results[scenario], path(kind));
	const native = read("native");
	const electron = read("electron");

	const value = (tree: Tree | undefined) =>
		typeof tree?.median === "number" ? tree.median / scale : null;

	const range = (tree: Tree | undefined): [number, number] | undefined =>
		typeof tree?.min === "number" && typeof tree.max === "number"
			? [tree.min / scale, tree.max / scale]
			: undefined;

	rows.push({
		metric,
		unit,
		better,
		native: value(native),
		electron: value(electron),
		nativeRange: range(native),
		electronRange: range(electron),
	});
}

const bundle = results.bundle;

const diskBytes = (kind: string, ...path: string[]) =>
	at(bundle, [kind, "sizes", ...path])?.diskBytes as number | undefined;

size("Download (dmg)", downloads.native?.bytes, downloads.electron?.bytes);

size(
	"App on disk",
	diskBytes("native", "bundle"),
	diskBytes("electron", "bundle"),
);

size(
	"Engine framework on disk",
	diskBytes("native", "parts", "Frameworks/PaneCore.framework"),
	diskBytes("electron", "parts", "Frameworks/Electron Framework.framework"),
);

for (const launch of ["cold", "warm"]) {
	median(
		`${launch === "cold" ? "Cold" : "Warm"} launch: window on screen`,
		"ms",
		"lower",
		"launch",
		(kind) => ["summary", kind, launch, "window"],
	);

	median(
		`${launch === "cold" ? "Cold" : "Warm"} launch: first page loaded`,
		"ms",
		"lower",
		"launch",
		(kind) => ["summary", kind, launch, "pageLoaded"],
	);
}

for (const tabs of ["1", "5", "10"]) {
	median(
		`Memory, ${tabs} tab${tabs === "1" ? "" : "s"}`,
		"MB",
		"lower",
		"memory",
		(kind) => ["summary", kind, tabs, "footprintSumBytes"],
		MB,
	);
}

for (const tabs of ["1", "10"]) {
	median(
		`Processes, ${tabs} tab${tabs === "1" ? "" : "s"}`,
		"",
		"lower",
		"memory",
		(kind) => ["summary", kind, tabs, "processes"],
	);
}

median("Idle CPU, one tab", "% of a core", "lower", "cpu", (kind) => [
	"summary",
	kind,
	"cpuPercent",
]);

median("Idle wakeups, one tab", "per s", "lower", "cpu", (kind) => [
	"summary",
	kind,
	"idleWakeups",
]);

median("Idle CPU energy, one tab", "mW", "lower", "cpu", (kind) => [
	"summary",
	kind,
	"milliwatts",
]);

const pageKeys = Object.keys(
	at(results.pages, ["summary", "native", "cold"]) ?? {},
);

// Largest contentful paint: a fixture's load event takes a few milliseconds
// either way, and a site's waits on what the site loads last.
for (const page of pageKeys) {
	const name = page.replace(/^https:\/\/(www\.)?/, "").replace(/\/$/, "");

	median(
		`${name}, empty cache: largest contentful paint`,
		"ms",
		"lower",
		"pages",
		(kind) => ["summary", kind, "cold", page, "largestContentfulPaint"],
	);
}

median("Speedometer 3.1", "score", "higher", "suites", (kind) => [
	"summary",
	kind,
	"speedometer",
]);

median("JetStream 3.0", "score", "higher", "suites", (kind) => [
	"summary",
	kind,
	"jetStream",
]);

// Whole milliseconds and counts; three significant figures otherwise.
function format(value: number | null, unit: string): string {
	if (value === null) {
		return "—";
	}

	if (value >= 100 || unit === "" || unit === "ms") {
		return value.toFixed(0);
	}

	return value.toFixed(value >= 10 ? 1 : 2);
}

// The median, and the runs' range when they differ.
function cell(
	value: number | null,
	range: [number, number] | undefined,
	unit: string,
): string {
	if (!range || format(range[0], unit) === format(range[1], unit)) {
		return format(value, unit);
	}

	return `${format(value, unit)} [${format(range[0], unit)}–${format(range[1], unit)}]`;
}

// How a lower value reads, then a higher one: a time is sooner or later, a
// size smaller or bigger, a count fewer or more, memory and the rest less or
// more.
function words(row: Row): [string, string] {
	if (row.unit === "ms") {
		return ["sooner", "later"];
	}

	if (row.unit === "" || row.unit === "per s") {
		return ["fewer", "more"];
	}

	if (row.unit === "MB" && !row.metric.startsWith("Memory")) {
		return ["smaller", "bigger"];
	}

	return ["less", "more"];
}

// The native app's change from the Electron app, as the native app's:
// "99.7 MB less (−41%)", "43 ms sooner (−11%)", "10% higher".
function change(row: Row): string {
	if (row.native === null || row.electron === null || row.electron === 0) {
		return "—";
	}

	const percent = Math.round((row.native / row.electron - 1) * 100);

	if (percent === 0) {
		return "the same (±0%)";
	}

	if (row.better === "higher") {
		return `${Math.abs(percent)}% ${percent > 0 ? "higher" : "lower"}`;
	}

	const [lower, higher] = words(row);

	// A unit short enough to read in the phrase; the others are in their
	// column.
	const unit = ["MB", "ms", "mW"].includes(row.unit) ? ` ${row.unit}` : "";
	const amount = format(Math.abs(row.native - row.electron), row.unit);

	return `${amount}${unit} ${percent < 0 ? lower : higher} (${percent < 0 ? "−" : "+"}${Math.abs(percent)}%)`;
}

const version = (kind: string) => at(bundle, [kind, "version"]) ?? "";

const table = [
	`| Metric | Unit | Better | Native ${version("native")} | Electron ${version("electron")} | Change |`,
	"| --- | --- | --- | ---: | ---: | --- |",
	...rows.map(
		(row) =>
			`| ${row.metric} | ${row.unit} | ${row.better} | ${cell(row.native, row.nativeRange, row.unit)} | ${cell(row.electron, row.electronRange, row.unit)} | ${change(row)} |`,
	),
].join("\n");

writeFileSync(
	out,
	`${JSON.stringify(
		{
			generatedAt: new Date().toISOString(),
			downloads,
			summary: rows,
			...results,
		},
		null,
		"\t",
	)}\n`,
);

console.log(table);
