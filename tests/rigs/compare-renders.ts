// Compares two runs' renders strictly: every PNG under each folder, by its
// path there, every byte of every pixel, no tolerance. Two runs of one build
// give identical renders, whatever the clock, the fixtures' ports, or the
// user's pointer do (ADR 0128), so any difference is a change of the build's
// or something a run still leaves to chance. Each render that differs is
// listed with its parts: its differing pixels, by 16-pixel squares that
// touch, each part's box, how many pixels, and the largest difference of a
// channel, with both runs' RGBA there.
//
//   bun tests/rigs/compare-renders.ts <run A's --out> <run B's --out>
//
// Exits 0 when both hold the same renders, each identical; 1 otherwise.
import { readdirSync, readFileSync } from "node:fs";
import { join } from "node:path";
import { differingParts, type Part, readPng } from "../lib/png";

const [first, second] = Bun.argv.slice(2);

if (!first || !second) {
	console.error("usage: compare-renders.ts <run A's folder> <run B's folder>");
	process.exit(2);
}

function renders(folder: string): Set<string> {
	return new Set(
		readdirSync(folder, { recursive: true, encoding: "utf8" }).filter((name) =>
			name.endsWith(".png"),
		),
	);
}

function describe(part: Part): string {
	return `${part.width}x${part.height} at ${part.x},${part.y}: ${part.pixels} px, largest ${part.largest} (${part.a.join(",")} against ${part.b.join(",")})`;
}

const inA = renders(first);
const inB = renders(second);
const both = [...inA].filter((name) => inB.has(name)).sort();
let identical = 0;
let differ = 0;

for (const name of both) {
	const bytesA = readFileSync(join(first, name));
	const bytesB = readFileSync(join(second, name));

	// One encoder over the same pixels writes the same file.
	if (bytesA.equals(bytesB)) {
		identical++;

		continue;
	}

	const a = readPng(bytesA);
	const b = readPng(bytesB);

	if (a.width !== b.width || a.height !== b.height) {
		differ++;

		console.log(
			`SIZE ${name}: ${a.width}x${a.height} against ${b.width}x${b.height}`,
		);

		continue;
	}

	const parts = differingParts(a, b);

	if (parts.length === 0) {
		identical++;

		continue;
	}

	differ++;

	console.log(
		`DIFF ${name}: ${parts.length} part${parts.length === 1 ? "" : "s"}`,
	);

	for (const part of parts) {
		console.log(`  ${describe(part)}`);
	}
}

const onlyA = [...inA].filter((name) => !inB.has(name)).sort();
const onlyB = [...inB].filter((name) => !inA.has(name)).sort();

for (const name of onlyA) {
	console.log(`ONLY A ${name}`);
}

for (const name of onlyB) {
	console.log(`ONLY B ${name}`);
}

console.log(
	`${both.length + onlyA.length + onlyB.length} renders: ${identical} identical, ${differ} differ, ${onlyA.length} only in A, ${onlyB.length} only in B`,
);

process.exit(differ + onlyA.length + onlyB.length === 0 ? 0 : 1);
