#!/usr/bin/env bun

import { readFileSync } from "node:fs";
import { join } from "node:path";
import { styleText } from "node:util";

const args = process.argv.slice(2);

if (args.length === 0) {
	console.error(
		styleText("red", "Usage: bun filter <package-pattern> <script> [args...]"),
	);

	console.error(styleText("gray", "Example: bun filter desktop dev"));
	process.exit(1);
}

const packagePattern = args[0];
const scriptName = args[1];

if (!scriptName) {
	console.error(styleText("red", "Please provide a script name to run"));
	process.exit(1);
}

const scriptArgs = args.slice(2);

const appsGlob = new Bun.Glob("apps/*/package.json");
const packagesGlob = new Bun.Glob("packages/*/package.json");

const packageJsonPaths = [
	...appsGlob.scanSync(process.cwd()),
	...packagesGlob.scanSync(process.cwd()),
];

const matchingPackages: { name: string; path: string }[] = [];

for (const pkgPath of packageJsonPaths) {
	const fullPath = join(process.cwd(), pkgPath);
	const pkgJson = JSON.parse(readFileSync(fullPath, "utf-8"));
	const pkgName = pkgJson.name as string;
	const pkgDir = pkgPath.replace("/package.json", "");

	if (
		pkgName.toLowerCase().includes(packagePattern.toLowerCase()) ||
		pkgDir.toLowerCase().includes(packagePattern.toLowerCase())
	) {
		matchingPackages.push({ name: pkgName, path: pkgDir });
	}
}

if (matchingPackages.length === 0) {
	console.error(
		styleText(
			"red",
			`No packages found matching pattern: ${styleText("yellow", packagePattern)}`,
		),
	);

	process.exit(1);
}

if (matchingPackages.length > 1) {
	console.error(
		styleText(
			"red",
			`Multiple packages match pattern "${styleText("yellow", packagePattern)}":`,
		),
	);

	for (const pkg of matchingPackages) {
		console.error(styleText("gray", `  - ${pkg.name} (${pkg.path})`));
	}

	console.error(styleText("red", "\nPlease use a more specific pattern."));

	process.exit(1);
}

const pkg = matchingPackages[0];
const command = ["run", scriptName, ...scriptArgs];

console.log(
	styleText("blue", `Executing: bun ${command.join(" ")} (in ${pkg.path})`),
);

console.log();

const proc = Bun.spawn(["bun", ...command], {
	stdout: "inherit",
	stderr: "inherit",
	stdin: "inherit",
	cwd: join(process.cwd(), pkg.path),
});

const exitCode = await proc.exited;

if (exitCode !== 0) {
	process.exit(exitCode);
}
