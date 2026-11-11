import { existsSync, mkdirSync, readdirSync, readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { launchPane, type PaneApp } from "../lib/pane";
import { pollValue } from "../lib/poll";
import { isRunning, pgrep, waitForExit } from "../lib/processes";
import { swiftTool } from "../lib/tools";
import {
	launchCopy,
	type StandInDmg,
	standInDmg,
	bundleVersion as version,
} from "../lib/update-copies";
import { expect, type Scenario } from "../run";

// Pane installing its own update (ADR 0092), against a stand-in release whose
// dmg holds a stand-in Pane: a tiny app with Pane's identifier and the offered
// version, which writes `reopened` into the data folder when it opens. Each
// run's Pane is a copy in a folder of its own, since the update replaces it.
// A dmg whose Pane is another version stays the user's to install; one that
// checks out is staged beside the app, swapped in at quit, and opened again
// by Restart to update, its old bundle and its dmg removed.
//
// PANE_UPDATE_DMG names a real dmg instead, as scripts/dist.sh builds it: the
// run's Pane installs it the same way (installRelease below).
const updateInstall: Scenario = async (context) => {
	const release = process.env.PANE_UPDATE_DMG;

	if (release) {
		return installRelease(context, release);
	}

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

		// Pane can't install it itself: it says how (Paper 06.3.1).
		expect(
			mismatched.update.label === "Quit and install",
			`the pill says ${mismatched.update.label}`,
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

		const pid = restart.app.browser.pid;
		await restart.app.send("restartToUpdate").catch(() => undefined);
		await exited(pid);

		const reopenedFile = join(restart.app.dataDir, "reopened");

		const reopened = await pollValue(
			() =>
				existsSync(reopenedFile) ? readFileSync(reopenedFile, "utf8") : "",
			(text) => text !== "",
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

		// One Pane can't install itself: Quit and install shows the installer
		// (opened here by no one, PANE_UPDATE_OPEN=0) and quits, the copy as
		// it was, for the user to drag the new one in.
		offer = dmgs.mismatched;
		const byHand = await launchCopy(context, `${origin}/releases/latest`);
		const before = version(byHand.bundle);
		await download(byHand.app);
		const byHandPid = byHand.app.browser.pid;
		await byHand.app.send("downloadUpdate").catch(() => undefined);
		await exited(byHandPid);

		expect(
			version(byHand.bundle) === before &&
				!existsSync(join(byHand.app.dataDir, "reopened")),
			`Quit and install left ${version(byHand.bundle)}, reopened: ${existsSync(join(byHand.app.dataDir, "reopened"))}`,
		);
	} finally {
		server.stop(true);
	}
};

async function download(app: PaneApp, timeoutMs = 30_000) {
	await app.waitFor(
		"an update on offer",
		(state) => state.update.status === "available",
		15_000,
	);

	await app.send("downloadUpdate");

	await app.waitFor(
		"the update downloaded",
		(state) => state.update.status === "downloaded",
		timeoutMs,
	);
}

function leftovers(bundle: string): string[] {
	return readdirSync(dirname(bundle)).filter((name) =>
		name.startsWith(".Pane-"),
	);
}

const exited = async (pid: number) =>
	expect(await waitForExit(pid, 30_000), `Pane (${pid}) did not quit`);

// The shell removes the old bundle once Pane has exited: a moment after.
const settle = (done: () => boolean) => pollValue(done, (gone) => gone, 5_000);

// A real release's dmg (scripts/dist.sh's), installed by the run's Pane, as a
// published Pane installs the next release (ADR 0092, ADR 0122;
// docs/plans/views-migration.md, K1): the published 0.19.1 installs the Views
// release, and a Views build the next. Its Pane has the run's identifier, and
// the copy reports an older version than it. A profile and its tab go before
// the update, and come back after it. Restart to update swaps it in and opens
// it on the same data; a Pane that opens it as a scenario's copy again (the
// Views app passes the mock keychain on) is read on the same channel, and
// timed from the press to its first window. A published Pane passes nothing:
// a test copy of the Views app opened that way refuses to start, and the
// runner opens it on the same data instead.
async function installRelease(context: Parameters<Scenario>[0], path: string) {
	const dmg = await releaseDmg(context, path);
	let origin = "";

	const server = Bun.serve({
		hostname: "127.0.0.1",
		port: 0,
		fetch(request): Response {
			const { pathname } = new URL(request.url);

			if (pathname === "/releases/latest") {
				return Response.json({
					tag_name: `v${dmg.version}`,
					published_at: "2026-11-11T00:00:00Z",
					assets: [
						{
							name: dmg.name,
							browser_download_url: `${origin}/download/${dmg.name}`,
							digest: `sha256:${dmg.sha256}`,
						},
					],
				});
			}

			return pathname === `/download/${dmg.name}`
				? new Response(Bun.file(dmg.path))
				: new Response("not found", { status: 404 });
		},
	});

	origin = server.url.origin;
	let reopenedPid: number | undefined;

	try {
		const copy = await launchCopy(
			context,
			`${origin}/releases/latest`,
			dmg.version,
		);

		const from = version(copy.bundle) || "0.0.0";
		const profile = await copy.app.createProfile("Kept");
		const page = context.fixtures.url("smoke.html?kept");
		await copy.app.send("openTab", { profile, url: page });

		await copy.app.waitFor(
			"the kept tab",
			(state) =>
				state.profiles.some(
					(each) =>
						each.id === profile && each.tabs.some((tab) => tab.url === page),
				),
			15_000,
		);

		const downloadStarted = Date.now();
		await download(copy.app, 120_000);
		const downloadMs = Date.now() - downloadStarted;
		const staged = await copy.app.state();

		expect(
			staged.update.installsItself &&
				staged.update.label === "Restart to update" &&
				leftovers(copy.bundle).includes(`.Pane-${dmg.version}.app`),
			`${dmg.version} was not staged: ${JSON.stringify(staged.update)}, beside: ${leftovers(copy.bundle).join(", ")}`,
		);

		// Restart to update, pressed: the swap, then the new Pane.
		const executable = join(copy.bundle, "Contents", "MacOS", "Pane");
		const pid = copy.app.browser.pid;
		const pressed = Date.now();
		await copy.app.send("restartToUpdate").catch(() => undefined);

		await pollValue(
			() => isRunning(pid),
			(running) => !running,
			30_000,
			5,
		);

		expect(!isRunning(pid), `Pane (${pid}) did not quit`);
		const quitMs = Date.now() - pressed;

		expect(
			version(copy.bundle) === dmg.version,
			`the bundle is ${version(copy.bundle)} after Restart to update`,
		);

		// Opened again, by the shell the swap left: its first window.
		const started = await pollValue(
			() => pgrep("-f", executable),
			(pids) => pids.length > 0,
			10_000,
			5,
		);

		reopenedPid = started[0];
		const startedMs = reopenedPid === undefined ? null : Date.now() - pressed;
		let firstWindowMs: number | null = null;

		if (reopenedPid !== undefined) {
			const waiter = await swiftTool("window-wait");

			const seen = JSON.parse(
				await Bun.$`${waiter} ${String(reopenedPid)} 15000`
					.quiet()
					.nothrow()
					.text(),
			) as { seenAt: number | null };

			firstWindowMs = seen.seenAt === null ? null : seen.seenAt - pressed;
		}

		await settle(() => leftovers(copy.bundle).length === 0);

		expect(
			leftovers(copy.bundle).length === 0,
			`left beside the app: ${leftovers(copy.bundle).join(", ")}`,
		);

		expect(
			!existsSync(join(copy.downloads, dmg.name)),
			"the installed update's dmg stayed in Downloads",
		);

		// The new Pane on the channel, or else opened by the runner.
		const reopened = firstWindowMs !== null;
		let app = copy.app;

		if (!reopened) {
			app = await launchPane(context, {
				binary: executable,
				dataDir: copy.app.dataDir,
				env: {
					PANE_UPDATE_URL: `${origin}/releases/latest`,
					PANE_UPDATE_DOWNLOADS: copy.downloads,
					PANE_UPDATE_OPEN: "0",
				},
			});
		}

		const opened = await app.waitFor(
			"the new Pane with the profiles open",
			(state) =>
				state.security.data.phase === "open" && state.security.data.attached,
			60_000,
		);

		const kept = opened.profiles.find((each) => each.id === profile);

		expect(
			kept?.tabs.some((tab) => tab.url === page),
			`the profile or its tab is gone: ${JSON.stringify(opened.profiles)}`,
		);

		const checked = await app.waitFor(
			"the new Pane's check",
			(state) => state.update.checked,
			15_000,
		);

		expect(
			checked.update.status === "idle" && !checked.update.pill,
			`the new Pane still offers an update: ${JSON.stringify(checked.update)}`,
		);

		if (reopened) {
			await app.send("quit").catch(() => undefined);

			expect(
				reopenedPid !== undefined && (await waitForExit(reopenedPid, 30_000)),
				"the reopened Pane did not quit",
			);
		}

		return {
			from,
			to: dmg.version,
			downloadMs,
			quitMs,
			startedMs,
			firstWindowMs,
			reopened,
		};
	} finally {
		// One the shell opened is the runner's to stop, not the user's.
		if (reopenedPid !== undefined && isRunning(reopenedPid)) {
			process.kill(reopenedPid, "SIGTERM");
			await waitForExit(reopenedPid, 15_000);
		}

		server.stop(true);
	}
}

// The release's dmg, read where it is: its Pane's version and identifier,
// from the dmg attached read-only at a folder of the scenario's, then
// detached; and its SHA-256, as GitHub gives it.
async function releaseDmg(
	context: Parameters<Scenario>[0],
	path: string,
): Promise<StandInDmg & { identifier: string }> {
	const mount = join(context.tempDir("pane-release-"), "mount");
	mkdirSync(mount);
	await Bun.$`diskutil image attach --readOnly --nobrowse --mountPoint ${mount} ${path}`.quiet();

	let plist: string;

	try {
		plist = readFileSync(
			join(mount, "Pane.app", "Contents", "Info.plist"),
			"utf8",
		);
	} finally {
		await Bun.$`diskutil eject ${mount}`.quiet().nothrow();
	}

	const value = (key: string) =>
		plist.match(
			new RegExp(`<key>${key}</key>\\s*<string>([^<]*)</string>`),
		)?.[1] ?? "";

	const hasher = new Bun.CryptoHasher("sha256");
	hasher.update(await Bun.file(path).arrayBuffer());
	const released = value("PaneVersion");

	expect(released !== "", "the dmg holds no Pane with a version");

	return {
		name: `Pane-${released}-arm64.dmg`,
		version: released,
		identifier: value("CFBundleIdentifier"),
		path,
		sha256: hasher.digest("hex"),
	};
}

export default updateInstall;
