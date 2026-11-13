import { createHash, randomBytes } from "node:crypto";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import type { ScenarioContext } from "../run";
import { appOf } from "./browser";
import { launchPane } from "./pane";
import {
	compileC,
	copyBundle,
	makeDmg,
	setPlistString,
	signAdHoc,
} from "./tools";

// What the update scenarios install from and into (ADR 0092): a stand-in
// release's dmg, holding a stand-in Pane, and a copy of the run's Pane in a
// folder of its own, since an update replaces the bundle it runs from. The
// Mac's: Windows' install and update are W7's (tools.ts says so there).

export type StandInDmg = {
	name: string;
	version: string;
	path: string;
	sha256: string;
};

// A dmg offered as `offered`, holding a stand-in Pane that says `holds`, with
// `padding` bytes of noise in it, so a slow server's download lasts.
export async function standInDmg(
	work: string,
	holds: string,
	offered: string,
	padding = 0,
): Promise<StandInDmg> {
	const root = join(work, `dmg-${offered}`);
	const contents = join(root, "Pane.app", "Contents");
	mkdirSync(join(contents, "MacOS"), { recursive: true });

	if (padding > 0) {
		mkdirSync(join(contents, "Resources"), { recursive: true });
		writeFileSync(join(contents, "Resources", "padding"), randomBytes(padding));
	}

	const keys = {
		CFBundleExecutable: "Pane",
		CFBundleIdentifier: "xyz.roundedcorners.pane",
		CFBundlePackageType: "APPL",
		CFBundleShortVersionString: holds,
		PaneVersion: holds,
	};

	const entries = Object.entries(keys).map(
		([key, value]) => `<key>${key}</key><string>${value}</string>`,
	);

	writeFileSync(
		join(contents, "Info.plist"),
		`<?xml version="1.0" encoding="UTF-8"?><!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd"><plist version="1.0"><dict>${entries.join("")}</dict></plist>`,
	);

	const source = join(work, "reopened.c");

	writeFileSync(
		source,
		'#include <stdio.h>\n#include <stdlib.h>\nint main(void) { const char *d = getenv("PANE_DATA_DIR"); char p[4096]; FILE *f; if (!d) return 1; snprintf(p, sizeof p, "%s/reopened", d); f = fopen(p, "w"); if (!f) return 1; fputs(VERSION, f); fclose(f); return 0; }\n',
	);

	await compileC(source, join(contents, "MacOS", "Pane"), [
		`VERSION="${holds}"`,
	]);

	await signAdHoc(join(root, "Pane.app"));

	const name = `Pane-${offered}-arm64.dmg`;
	const path = join(work, name);
	await makeDmg(root, path, "Pane");

	return {
		name,
		version: offered,
		path,
		sha256: createHash("sha256").update(readFileSync(path)).digest("hex"),
	};
}

// A copy of the run's Pane in a folder of its own, checking `releases`, with
// `env` too. Given `olderThan`, a copy that reports that version or a later
// one is made to report 0.0.1, its signature made again with what it had (the
// hardened runtime, its entitlements), as the live update check makes a
// published Pane older.
export async function launchCopy(
	context: ScenarioContext,
	releases: string,
	olderThan?: string,
	env: Record<string, string> = {},
) {
	const folder = context.tempDir("pane-apps-");
	const bundle = join(folder, "Pane.app");
	await copyBundle(appOf(context.binary), bundle);

	if (olderThan && !isOlder(bundleVersion(bundle) || "0.0.0", olderThan)) {
		const plist = join(bundle, "Contents", "Info.plist");
		await setPlistString(plist, "PaneVersion", "0.0.1");
		await signAdHoc(bundle, true);
	}

	const downloads = context.tempDir("pane-downloads-");

	const app = await launchPane(context, {
		binary: join(bundle, "Contents", "MacOS", "Pane"),
		env: {
			PANE_UPDATE_URL: releases,
			PANE_UPDATE_DOWNLOADS: downloads,
			PANE_UPDATE_OPEN: "0",
			...env,
		},
	});

	return { app, bundle, downloads };
}

// The version a bundle says it is (PaneVersion), or none.
export function bundleVersion(bundle: string): string {
	const plist = readFileSync(join(bundle, "Contents", "Info.plist"), "utf8");

	return (
		plist.match(/<key>PaneVersion<\/key>\s*<string>([^<]*)<\/string>/)?.[1] ??
		""
	);
}

// Whether version `a` comes before `b`, part by part.
function isOlder(a: string, b: string): boolean {
	const left = a.split(".").map(Number);
	const right = b.split(".").map(Number);

	for (let part = 0; part < Math.max(left.length, right.length); part++) {
		const difference = (left[part] ?? 0) - (right[part] ?? 0);

		if (difference !== 0) {
			return difference < 0;
		}
	}

	return false;
}
