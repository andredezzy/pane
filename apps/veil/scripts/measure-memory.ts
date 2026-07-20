// Snapshot of Pane's per-process resident memory, grouped by Chromium process
// type (parsed from each helper's --type/--utility-sub-type flags; the process
// without a --type flag is Chromium's browser process — Electron's main).
//
// Usage: bun scripts/measure-memory.ts [match]
//   match — substring selecting the app's processes (default "Pane.app";
//   pass "electron/dist" to measure an electron-vite dev instance).
//
// Baseline workload for comparable runs: 3 profiles loaded, 5 tabs each,
// window visible, 2 minutes idle after the last tab finishes loading.
import { $ } from "bun";

interface ProcessGroup {
	count: number;
	rssKb: number;
}

const match = process.argv[2] ?? "Pane.app";
const psOutput = await $`ps -axo rss=,command=`.text();
const groups = new Map<string, ProcessGroup>();

for (const line of psOutput.split("\n")) {
	const parsed = /^\s*(\d+)\s+(.*)$/.exec(line);

	if (!parsed || !parsed[2].includes(match)) {
		continue;
	}

	const rssKb = Number(parsed[1]);
	const type = /--type=(\S+)/.exec(parsed[2])?.[1] ?? "browser";
	const subType = /--utility-sub-type=(\S+)/.exec(parsed[2])?.[1];
	const key = subType ? `${type} (${subType})` : type;
	const group = groups.get(key) ?? { count: 0, rssKb: 0 };

	group.count += 1;
	group.rssKb += rssKb;
	groups.set(key, group);
}

if (groups.size === 0) {
	console.error(`No processes matching "${match}" — is the app running?`);
	process.exit(1);
}

const rows = [...groups.entries()].sort((a, b) => b[1].rssKb - a[1].rssKb);
let totalKb = 0;

console.log(new Date().toISOString());

for (const [key, { count, rssKb }] of rows) {
	totalKb += rssKb;

	console.log(`${(rssKb / 1024).toFixed(0).padStart(6)} MB  x${count}  ${key}`);
}

console.log(`${(totalKb / 1024).toFixed(0).padStart(6)} MB  TOTAL`);
