import { createHash } from "node:crypto";
import {
	existsSync,
	mkdirSync,
	readdirSync,
	readFileSync,
	writeFileSync,
} from "node:fs";
import { dirname, join } from "node:path";
import { $ } from "bun";
import { launchPane, type PaneApp } from "../lib/pane";
import { expect, type Scenario } from "../run";

// Pane installing its own update (ADR 0092), against a stand-in release whose
// dmg holds a stand-in Pane: a tiny app with Pane's identifier and the offered
// version, which writes `reopened` into the data folder when it opens. Each
// run's Pane is a copy in a folder of its own, since the update replaces it.
// A dmg whose Pane is another version stays the user's to install; one that
// checks out is staged beside the app, swapped in at quit, and opened again
// by Restart to update, its old bundle and its dmg removed.
const updateInstall: Scenario = async (context) => {
	const work = context.tempDir("pane-update-install-");

	const dmgs = {
		// Offered as 98.0.0, holding a Pane that says 97.0.0.
		mismatched: await standInDmg(work, "97.0.0", "98.0.0"),
		good: await standInDmg(work, "99.0.0", "99.0.0"),
	};

	let offer = dmgs.mismatched;
	let origin = "";

	const server = Bun.serve({
		hostname: "127.0.0.1",
		port: 0,
		fetch(request): Response {
			const { pathname } = new URL(request.url);

			if (pathname === "/releases/latest") {
				return Response.json({
					tag_name: `v${offer.version}`,
					published_at: "2026-10-29T00:00:00Z",
					assets: [
						{
							name: offer.name,
							browser_download_url: `${origin}/download/${offer.name}`,
							digest: `sha256:${offer.sha256}`,
						},
					],
				});
			}

			const dmg = Object.values(dmgs).find(
				(candidate) => pathname === `/download/${candidate.name}`,
			);

			return dmg
				? new Response(Bun.file(dmg.path))
				: new Response("not found", { status: 404 });
		},
	});

	origin = server.url.origin;

	try {
		// Restart to update: the swap, then the new Pane opens.
		const restart = await launchCopy(context, `${origin}/releases/latest`);

		await download(restart.app);
		const mismatched = await restart.app.state();

		expect(
			!mismatched.update.installsItself,
			"a dmg holding another version was staged",
		);

		expect(
			leftovers(restart.bundle).length === 0,
			`a mismatched dmg left ${leftovers(restart.bundle).join(", ")}`,
		);

		offer = dmgs.good;
		await restart.app.send("checkUpdates");

		await restart.app.waitFor(
			"v99.0.0 on offer",
			(state) => state.update.available === "99.0.0",
			10_000,
		);

		await download(restart.app);
		const staged = await restart.app.state();
		expect(staged.update.installsItself, "the matching dmg was not staged");
		const besideBeforeQuit = readdirSync(dirname(restart.bundle));

		const pid = restart.app.browser.pid;
		await restart.app.send("restartToUpdate").catch(() => undefined);
		await exited(pid);

		const reopened = await waitForFile(
			join(restart.app.dataDir, "reopened"),
			15_000,
		);

		expect(
			version(restart.bundle) === "99.0.0",
			`the bundle is ${version(restart.bundle)} after Restart to update`,
		);

		expect(reopened === "99.0.0", "the new Pane did not open on the same data");
		await settle(() => leftovers(restart.bundle).length === 0);

		expect(
			leftovers(restart.bundle).length === 0,
			`left beside the app: ${leftovers(restart.bundle).join(", ")}`,
		);

		// The installed one's dmg goes; the mismatched one stays the user's.
		expect(
			!existsSync(join(restart.downloads, dmgs.good.name)),
			"the installed update's dmg stayed in Downloads",
		);

		expect(
			existsSync(join(restart.downloads, dmgs.mismatched.name)),
			"the dmg left to the user was removed",
		);

		// A plain quit installs too, and opens nothing.
		const quit = await launchCopy(context, `${origin}/releases/latest`);
		await download(quit.app);

		expect(
			(await quit.app.state()).update.installsItself,
			"the second copy did not stage the update",
		);

		const quitPid = quit.app.browser.pid;
		await quit.app.send("quit").catch(() => undefined);
		await exited(quitPid);
		await Bun.sleep(1_500);

		expect(
			version(quit.bundle) === "99.0.0",
			`the bundle is ${version(quit.bundle)} after a quit`,
		);

		expect(
			!existsSync(join(quit.app.dataDir, "reopened")),
			"a plain quit opened the new Pane",
		);

		await settle(() => leftovers(quit.bundle).length === 0);

		expect(
			leftovers(quit.bundle).length === 0,
			`left beside the app: ${leftovers(quit.bundle).join(", ")}`,
		);

		return {
			besideBeforeQuit,
			restarted: version(restart.bundle),
			quit: version(quit.bundle),
		};
	} finally {
		server.stop(true);
	}
};

type StandInDmg = {
	name: string;
	version: string;
	path: string;
	sha256: string;
};

// A dmg offered as `offered`, holding a stand-in Pane that says `holds`.
async function standInDmg(
	work: string,
	holds: string,
	offered: string,
): Promise<StandInDmg> {
	const root = join(work, `dmg-${offered}`);
	const contents = join(root, "Pane.app", "Contents");
	mkdirSync(join(contents, "MacOS"), { recursive: true });

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

	await $`clang -O2 -DVERSION=${`"${holds}"`} -o ${join(contents, "MacOS", "Pane")} ${source}`.quiet();
	await $`codesign --force --sign - ${join(root, "Pane.app")}`.quiet();

	const name = `Pane-${offered}-arm64.dmg`;
	const path = join(work, name);
	await $`hdiutil create -quiet -ov -fs HFS+ -volname Pane -srcfolder ${root} ${path}`.quiet();

	return {
		name,
		version: offered,
		path,
		sha256: createHash("sha256").update(readFileSync(path)).digest("hex"),
	};
}

// A copy of the run's Pane in a folder of its own, checking `releases`.
async function launchCopy(context: Parameters<Scenario>[0], releases: string) {
	const folder = context.tempDir("pane-apps-");
	const bundle = join(folder, "Pane.app");
	await $`ditto ${context.binary.replace(/\/Contents\/MacOS\/Pane$/, "")} ${bundle}`.quiet();

	const downloads = context.tempDir("pane-downloads-");

	const app = await launchPane(context, {
		binary: join(bundle, "Contents", "MacOS", "Pane"),
		env: {
			PANE_UPDATE_URL: releases,
			PANE_UPDATE_DOWNLOADS: downloads,
			PANE_UPDATE_OPEN: "0",
		},
	});

	return { app, bundle, downloads };
}

async function download(app: PaneApp) {
	await app.waitFor(
		"an update on offer",
		(state) => state.update.status === "available",
		15_000,
	);

	await app.send("downloadUpdate");

	await app.waitFor(
		"the update downloaded",
		(state) => state.update.status === "downloaded",
		30_000,
	);
}

function version(bundle: string): string {
	const plist = readFileSync(join(bundle, "Contents", "Info.plist"), "utf8");

	return (
		plist.match(/<key>PaneVersion<\/key>\s*<string>([^<]*)<\/string>/)?.[1] ??
		""
	);
}

function leftovers(bundle: string): string[] {
	return readdirSync(dirname(bundle)).filter((name) =>
		name.startsWith(".Pane-"),
	);
}

async function exited(pid: number) {
	const deadline = Date.now() + 30_000;

	while (Date.now() < deadline) {
		try {
			process.kill(pid, 0);
		} catch {
			return;
		}

		await Bun.sleep(100);
	}

	throw new Error(`Pane (${pid}) did not quit`);
}

async function waitForFile(path: string, timeoutMs: number): Promise<string> {
	const deadline = Date.now() + timeoutMs;

	while (Date.now() < deadline) {
		if (existsSync(path)) {
			return readFileSync(path, "utf8");
		}

		await Bun.sleep(100);
	}

	return "";
}

// The shell removes the old bundle once Pane has exited: a moment after.
async function settle(done: () => boolean) {
	const deadline = Date.now() + 5_000;

	while (!done() && Date.now() < deadline) {
		await Bun.sleep(100);
	}
}

export default updateInstall;
