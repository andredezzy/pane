import {
	closeSync,
	existsSync,
	mkdirSync,
	openSync,
	readdirSync,
	readFileSync,
} from "node:fs";
import { join } from "node:path";
import { dataOpen, launchPane, type PaneApp, QUIT_WORD } from "../lib/pane";
import { pollValue } from "../lib/poll";
import {
	holdOpen,
	isRunning,
	pidsMatching,
	waitForExit,
} from "../lib/processes";
import { swiftTool } from "../lib/tools";
import {
	executableOf,
	launchCopy,
	updateLeftovers as leftovers,
	type RestartTimeline,
	reopenedFrom,
	STAND_IN_IS_PANE,
	type StandInDmg,
	SWAP_AFTER_EXIT,
	stagedVersion,
	standInRelease,
	bundleVersion as version,
	watchRestart,
} from "../lib/update-copies";
import { expect, type Scenario } from "../run";

// Pane installing its own update (ADR 0092; on Windows ADR 0127), against a
// stand-in release: on the Mac a dmg holding a stand-in Pane, a tiny app with
// Pane's identifier and the offered version, which writes `reopened` into the
// data folder when it opens; on Windows Pane's own installer, its payload the
// run's app folder at the offered version. Each run's Pane is a copy in a
// folder of its own, since the update replaces it. A file whose Pane is
// another version stays the user's to install; one that checks out is staged
// beside the app, swapped in at quit (on Windows by the helper, once Pane has
// exited), and opened again by Restart to update, the old app and the file
// removed. A newer release replaces what is staged: the copy and its file go,
// a quit while the newer one downloads installs nothing, and the newer one,
// staged in turn, installs. Where the swap runs once Pane has exited
// (Windows'), a file of the app held open fails it: it rolls back, Pane opens
// as it was and says so above the pill (Paper 11.12.14, E), and the next
// exit installs it.
//
// PANE_UPDATE_DMG names a real dmg instead, as scripts/dist.sh builds it: the
// run's Pane installs it the same way (installRelease below).
const updateInstall: Scenario = async (context) => {
	const release = process.env.PANE_UPDATE_DMG;

	if (release) {
		return installRelease(context, release);
	}

	const work = context.tempDir("pane-update-install-");

	const standIn = (holds: string, offered: string) =>
		standInRelease(context.binary, work, holds, offered);

	const dmgs = {
		// Offered as 98.0.0, holding a Pane that says 97.0.0.
		mismatched: await standIn("97.0.0", "98.0.0"),
		good: await standIn("99.0.0", "99.0.0"),
		// Offered once 99.0.0 is staged.
		newer: await standIn("100.0.0", "100.0.0"),
	};

	let offer = dmgs.mismatched;
	let origin = "";
	// While set, the newer dmg's download waits for it: a quit comes mid-way.
	let held: Promise<void> | undefined;

	const server = Bun.serve({
		hostname: "127.0.0.1",
		port: 0,
		fetch(request): Response | Promise<Response> {
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

			if (!dmg) {
				return new Response("not found", { status: 404 });
			}

			const file = () => new Response(Bun.file(dmg.path));

			return dmg === dmgs.newer && held ? held.then(file) : file();
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
			"a file holding another version was staged",
		);

		// Pane can't install it itself: it says how (Paper 06.3.1, 11.12.11).
		expect(
			mismatched.update.label === `${QUIT_WORD} and install`,
			`the pill says ${mismatched.update.label}`,
		);

		expect(
			leftovers(restart.bundle).length === 0,
			`a mismatched file left ${leftovers(restart.bundle).join(", ")}`,
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
		expect(staged.update.installsItself, "the matching file was not staged");

		// A real Pane opens again (Windows'): a profile and its tab go before
		// the restart, and come back after it.
		const kept = STAND_IN_IS_PANE
			? await keepAProfile(context, restart.app)
			: undefined;

		const pid = restart.app.browser.pid;

		const watched = STAND_IN_IS_PANE
			? await watchRestart(
					pid,
					restart.bundle,
					"99.0.0",
					join(context.tempDir("pane-restart-"), "ready"),
				)
			: undefined;

		const pressed = Date.now();
		await restart.app.send("restartToUpdate").catch(() => undefined);
		await exited(pid);

		const reopenedFile = join(restart.app.dataDir, "reopened");
		let timing: Record<string, unknown> | undefined;

		if (kept && watched) {
			const opened = await restart.app.waitFor(
				"the new Pane with the profiles open",
				dataOpen,
				60_000,
			);

			expect(
				opened.profiles.some(
					(each) =>
						each.id === kept.profile &&
						each.tabs.some((tab) => tab.url === kept.page),
				),
				`the profile or its tab is gone: ${JSON.stringify(opened.profiles)}`,
			);

			expect(
				!opened.update.pill && !opened.update.installsItself,
				`the new Pane still offers an update: ${JSON.stringify(opened.update)}`,
			);

			timing = {
				...since(pressed, await watched.seen),
				helper: swapNotes(restart.app),
			};
		} else {
			const reopened = await pollValue(
				() =>
					existsSync(reopenedFile) ? readFileSync(reopenedFile, "utf8") : "",
				(text) => text !== "",
				15_000,
			);

			expect(
				reopened === "99.0.0",
				"the new Pane did not open on the same data",
			);
		}

		expect(
			version(restart.bundle) === "99.0.0",
			`the app is ${version(restart.bundle)} after Restart to update`,
		);

		// The installed one's file goes after the swap.
		await settle(
			() =>
				leftovers(restart.bundle).length === 0 &&
				!existsSync(join(restart.downloads, dmgs.good.name)),
		);

		expect(
			leftovers(restart.bundle).length === 0,
			`left beside the app: ${leftovers(restart.bundle).join(", ")}; the helper: ${swapNotes(restart.app)}`,
		);

		// The installed one's file goes; the mismatched one stays the user's.
		expect(
			!existsSync(join(restart.downloads, dmgs.good.name)),
			"the installed update's file stayed in Downloads",
		);

		expect(
			existsSync(join(restart.downloads, dmgs.mismatched.name)),
			"the file left to the user was removed",
		);

		if (kept) {
			await quitReopened(restart.app, restart.bundle);
		}

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
		await settle(() => version(quit.bundle) === "99.0.0");
		await Bun.sleep(1_500);

		expect(
			version(quit.bundle) === "99.0.0",
			`the app is ${version(quit.bundle)} after a quit`,
		);

		expect(
			!(await reopenedFrom(quit.bundle, quit.app.dataDir)),
			"a plain quit opened the new Pane",
		);

		await settle(() => leftovers(quit.bundle).length === 0);

		expect(
			leftovers(quit.bundle).length === 0,
			`left beside the app: ${leftovers(quit.bundle).join(", ")}; the helper: ${swapNotes(quit.app)}`,
		);

		// Where the swap runs once Pane has exited, a file of the app held open
		// fails it: it rolls back, and the Pane the restart opens is the old
		// one, saying so above the pill and in Settings (Paper 11.12.14, E),
		// still offering Restart to update; the next exit installs it.
		const failed = SWAP_AFTER_EXIT
			? await failedSwap(context, `${origin}/releases/latest`)
			: undefined;

		// One Pane can't install itself: Quit and install shows the installer
		// (opened here by no one, PANE_UPDATE_OPEN=0) and quits, the copy as
		// it was, for the user to install the new one.
		offer = dmgs.mismatched;
		const byHand = await launchCopy(context, `${origin}/releases/latest`);
		const before = version(byHand.bundle);
		await download(byHand.app);
		const byHandPid = byHand.app.browser.pid;
		await byHand.app.send("downloadUpdate").catch(() => undefined);
		await exited(byHandPid);
		await Bun.sleep(1_500);
		const byHandOpened = await reopenedFrom(byHand.bundle, byHand.app.dataDir);

		expect(
			version(byHand.bundle) === before && !byHandOpened,
			`${QUIT_WORD} and install left ${version(byHand.bundle)}, reopened: ${byHandOpened}`,
		);

		// A newer release replaces what is staged: staged 99.0.0, then offered
		// 100.0.0, the staged copy and its file go, and 100.0.0 is offered as
		// any update is. A quit while it downloads installs nothing. Opened
		// again, the copy stages 100.0.0 in turn, which a check offering it
		// again keeps, and a quit installs it.
		offer = dmgs.good;
		const newer = await launchCopy(context, `${origin}/releases/latest`);
		const own = version(newer.bundle);

		const inDownloads = (dmg: StandInDmg) =>
			existsSync(join(newer.downloads, dmg.name));

		await download(newer.app);

		expect(
			(await newer.app.state()).update.installsItself &&
				stagedVersion(newer.bundle, "99.0.0") === "99.0.0" &&
				inDownloads(dmgs.good),
			`99.0.0 was not staged: beside ${leftovers(newer.bundle).join(", ")}`,
		);

		let letGo = () => {};

		held = new Promise((resolve) => {
			letGo = resolve;
		});

		offer = dmgs.newer;
		await newer.app.send("checkUpdates");

		const offered = await newer.app.waitFor(
			"v100.0.0 on offer",
			(state) => state.update.available === "100.0.0",
			10_000,
		);

		expect(
			offered.update.status === "available" &&
				offered.update.label === "Update available" &&
				offered.update.pill &&
				!offered.update.installsItself,
			`the newer offer: ${JSON.stringify(offered.update)}`,
		);

		await settle(
			() => leftovers(newer.bundle).length === 0 && !inDownloads(dmgs.good),
		);

		expect(
			leftovers(newer.bundle).length === 0 && !inDownloads(dmgs.good),
			`the replaced update left ${[...leftovers(newer.bundle), ...readdirSync(newer.downloads)].join(", ")}`,
		);

		await newer.app.send("downloadUpdate");

		await newer.app.waitFor(
			"v100.0.0 downloading",
			(state) => state.update.status === "downloading",
			5_000,
		);

		const newerPid = newer.app.browser.pid;
		await newer.app.send("quit").catch(() => undefined);
		await exited(newerPid);
		held = undefined;
		letGo();
		await Bun.sleep(1_500);

		expect(
			version(newer.bundle) === own &&
				!(await reopenedFrom(newer.bundle, newer.app.dataDir)),
			`a quit mid-download left the app at ${version(newer.bundle)}`,
		);

		expect(
			leftovers(newer.bundle).length === 0 &&
				readdirSync(newer.downloads).length === 0,
			`a quit mid-download left ${[...leftovers(newer.bundle), ...readdirSync(newer.downloads)].join(", ")}`,
		);

		const again = await launchPane(context, {
			binary: executableOf(newer.bundle),
			dataDir: newer.app.dataDir,
			env: {
				PANE_UPDATE_URL: `${origin}/releases/latest`,
				PANE_UPDATE_DOWNLOADS: newer.downloads,
				PANE_UPDATE_OPEN: "0",
				...(SWAP_AFTER_EXIT && { PANE_UPDATE_INSTALL: "1" }),
			},
		});

		await download(again);
		await again.send("checkUpdates");
		await Bun.sleep(1_200);
		const keptStaged = (await again.state()).update;

		expect(
			keptStaged.status === "downloaded" &&
				keptStaged.installsItself &&
				keptStaged.label === "Restart to update" &&
				stagedVersion(newer.bundle, "100.0.0") === "100.0.0" &&
				leftovers(newer.bundle).filter((name) => !name.endsWith(".download"))
					.length === 1,
			`100.0.0 staged: ${JSON.stringify(keptStaged)}, beside ${leftovers(newer.bundle).join(", ")}`,
		);

		const againPid = again.browser.pid;
		await again.send("quit").catch(() => undefined);
		await exited(againPid);

		await settle(
			() => leftovers(newer.bundle).length === 0 && !inDownloads(dmgs.newer),
		);

		expect(
			version(newer.bundle) === "100.0.0" &&
				!(await reopenedFrom(newer.bundle, newer.app.dataDir)),
			`the quit left the app at ${version(newer.bundle)}`,
		);

		expect(
			leftovers(newer.bundle).length === 0 && !inDownloads(dmgs.newer),
			`left after the install: ${[...leftovers(newer.bundle), ...readdirSync(newer.downloads)].join(", ")}`,
		);

		return timing || failed
			? { restart: timing, failedSwap: failed }
			: undefined;
	} finally {
		server.stop(true);
	}
};

// Downloaded, and staged where it is: on Windows a stand-in holds the whole
// app folder, 472 MB, which the VM's download, digest, and unpack take a
// while over.
async function download(app: PaneApp, timeoutMs = 180_000) {
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

// What Windows' helper noted of its run, each step and when, for a copy in
// the temporary folder (pane/installer/swap.h's NoteSwapForScenario); none on
// the Mac.
const swapNotes = (app: PaneApp) => {
	const file = join(app.automationDir, "swap-notes.txt");

	return existsSync(file) ? readFileSync(file, "utf8").trim() : "";
};

const exited = async (pid: number) =>
	expect(await waitForExit(pid, 30_000), `Pane (${pid}) did not quit`);

// The shell, or Windows' helper, removes the old app once Pane has exited: a
// moment after.
const settle = (done: () => boolean) => pollValue(done, (gone) => gone, 15_000);

// A profile and its tab in `app`, as a Restart to update must bring back.
async function keepAProfile(context: Parameters<Scenario>[0], app: PaneApp) {
	const profile = await app.createProfile("Kept");
	const page = context.fixtures.url("smoke.html?kept");
	await app.send("openTab", { profile, url: page });

	await app.waitFor(
		"the kept tab",
		(state) =>
			state.profiles.some(
				(each) =>
					each.id === profile && each.tabs.some((tab) => tab.url === page),
			),
		15_000,
	);

	return { profile, page };
}

// The moments of a restart in ms after its press.
function since(pressed: number, seen: RestartTimeline) {
	const after = (at: number | null) => (at === null ? null : at - pressed);

	return {
		windowGoneMs: after(seen.windowGone),
		quitMs: after(seen.quit),
		swappedMs: after(seen.swapped),
		startedMs: after(seen.started),
		firstWindowMs: after(seen.window),
		pointerMs: seen.pointer.map(([from, to]) => [from - pressed, to - pressed]),
	};
}

// The Pane the swap opened, quit on the channel, and gone with what ran from
// its folder.
async function quitReopened(app: PaneApp, bundle: string) {
	await app.send("quit").catch(() => undefined);

	await pollValue(
		() => reopenedFrom(bundle, app.dataDir),
		(running) => !running,
		30_000,
		250,
	);
}

// Windows' failed swap (11.12.14, E): a copy stages 99.0.0, a file of its app
// is held open by another process without FILE_SHARE_DELETE, and Restart to
// update fails and rolls back, opening the old Pane, which says so above the
// pill and in Settings and still offers Restart to update. Once the file is
// let go, the next exit installs it, and nothing is left behind.
async function failedSwap(context: Parameters<Scenario>[0], releases: string) {
	const copy = await launchCopy(context, releases);
	const own = version(copy.bundle);
	await download(copy.app);

	expect(
		(await copy.app.state()).update.installsItself,
		"the held copy did not stage the update",
	);

	const heldFile = join(copy.bundle, "PanePDF.ico");
	const holder = holdOpen(heldFile);

	try {
		// Held once it can no longer be opened to write.
		await pollValue(
			() => {
				try {
					closeSync(openSync(heldFile, "r+"));

					return false;
				} catch {
					return true;
				}
			},
			(held) => held,
			30_000,
			100,
		);

		const pid = copy.app.browser.pid;
		await copy.app.send("restartToUpdate").catch(() => undefined);
		await exited(pid);

		const back = await copy.app.waitFor(
			"the old Pane opened again, saying the update didn't install",
			(state) =>
				dataOpen(state) && state.update.installFailed && state.update.pill,
			60_000,
		);

		expect(
			version(copy.bundle) === own &&
				back.update.label === "Restart to update" &&
				back.update.installsItself &&
				stagedVersion(copy.bundle, "99.0.0") === "99.0.0",
			`after the failed swap: the app at ${version(copy.bundle)}, ${JSON.stringify(back.update)}, beside ${leftovers(copy.bundle).join(", ")}`,
		);

		await copy.app.render("update-not-installed.png");
		await copy.app.send("showPage", { page: "settings" });
		await copy.app.waitFor("Settings", (state) => state.page === "settings");
		await copy.app.render("update-not-installed-settings.png");
	} finally {
		holder.kill();
		await holder.exited;
	}

	// The next exit installs it, and opens nothing.
	await copy.app.send("quit").catch(() => undefined);
	await settle(() => version(copy.bundle) === "99.0.0");
	await settle(() => leftovers(copy.bundle).length === 0);

	expect(
		version(copy.bundle) === "99.0.0" &&
			!(await reopenedFrom(copy.bundle, copy.app.dataDir)),
		`the next exit left the app at ${version(copy.bundle)}`,
	);

	expect(
		leftovers(copy.bundle).length === 0 &&
			readdirSync(copy.downloads).length === 0,
		`left after the next exit: ${[...leftovers(copy.bundle), ...readdirSync(copy.downloads)].join(", ")}`,
	);

	return { installedAtTheNextExit: true };
}

// A real release's dmg (scripts/dist.sh's), installed by the run's Pane, as a
// published Pane installs the next release (ADR 0092, ADR 0122): the published
// 0.19.1 installs the Views release, and a Views build the next. Its Pane has
// the run's identifier, and the copy reports an older version than it. A
// profile and its tab go before the update, and come back after it. Restart to
// update swaps it in and opens it on the same data; a Pane that opens it as a
// scenario's copy again (the Views app passes the mock keychain on) is read on
// the same channel, and timed from the press to its first window. A published
// Pane passes nothing: a test copy of the Views app opened that way refuses to
// start, and the runner opens it on the same data instead.
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
			() => pidsMatching(executable),
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
