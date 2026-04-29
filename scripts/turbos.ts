#!/usr/bin/env bun

import { styleText } from "node:util";

const args = process.argv.slice(2);

if (args.length === 0) {
	console.error(styleText("red", "Please provide at least one filter"));
	process.exit(1);
}

const filters = args[0].split(",");

const newArgs: string[] = ["run"];

for (const filter of filters) {
	const hasDeps = filter.endsWith("...");
	const cleanFilter = hasDeps ? filter.slice(0, -3) : filter;
	const suffix = hasDeps ? "..." : "";
	newArgs.push(`--filter=*${cleanFilter}${suffix}`);
}

newArgs.push(...args.slice(1));

console.log(styleText("blue", `Executing: bun turbo ${newArgs.join(" ")}`));
console.log();

const proc = Bun.spawn(["bun", "turbo", ...newArgs], {
	stdout: "inherit",
	stderr: "inherit",
	stdin: "inherit",
});

const exitCode = await proc.exited;

if (exitCode !== 0) {
	process.exit(exitCode);
}
