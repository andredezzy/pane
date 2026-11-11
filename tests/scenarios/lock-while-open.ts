import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { launchPane, tabOf } from "../lib/pane";
import { poll } from "../lib/poll";
import {
	imagesAttachedIn,
	mountedAt,
	pgrep,
	waitForExit,
} from "../lib/processes";
import { expect, type Scenario } from "../run";

// Pane locks while open (ADR 0116, Paper 06.4 and 07.9). Lock Pane shows the
// PIN screen at once; behind it the profiles leave memory, each closes in
// the engine, and the volume detaches. The PIN opens them again, and the tab
// that showed comes back, still signed in. A download under way holds its
// profile open in Chrome: the lock stops it, as a quit does, and the volume
// still detaches whole, never forced from under the engine. With Lock after
// set, Pane locks by itself once it has gone that long without use (a minute
// a second here), and so it does when the displays sleep or another user takes
// the session. A lock that comes as the profile is still opening detaches as
// well, and Chrome's background work for that profile, held past the detach
// by a busy Mac or by Chrome's own startup, runs first, the keys still there.
// One watchdog serves a Pane through every lock, and still detaches the image
// when Pane is killed. A file something else holds open is never pulled away:
// the lock waits, and past 10 s Pane quits.

// bcryptjs 3.0.3, hashSync("1234", 10): a PIN as the Electron app set it.
const PIN_1234 = {
	state: {
		pin: {
			hash: "$2b$10$6RNvGASNquZ6f5LXjFap1eMcHOZTOzRYJVRe1DrjRG2juL90W5MNO",
			length: 4,
		},
		failedAttempts: 0,
	},
	version: 0,
};

const lockWhileOpen: Scenario = async (context) => {
	const dataDir = context.tempDir("pane-lock-");
	mkdirSync(dataDir, { recursive: true });
	writeFileSync(join(dataDir, "security.json"), JSON.stringify(PIN_1234));
	const mountPoint = join(dataDir, "Engine", "Volume");

	const mounted = () => mountedAt(mountPoint);

	const app = await launchPane(context, {
		dataDir,
		env: { PANE_TIME_SCALE: "60" },
	});

	await app.send("setLockAfter", { minutes: null });
	await app.send("pinKeys", { keys: "1234" });

	await app.waitFor(
		"unlocked, the volume attached",
		(state) => !state.security.locked && state.security.data.attached,
		60_000,
	);

	const profile = await app.createProfile("Work", "BLUE");
	const url = context.fixtures.url("sign-in");
	const { tab, page } = await app.openPage(profile, url);
	await page.evaluate("window.ready");

	// A download under way: 4 MB over 4 s, from the release stand-in.
	const release = (await (
		await fetch(context.fixtures.url("releases/latest"))
	).json()) as { assets: { browser_download_url: string }[] };

	const slow = release.assets[0].browser_download_url;

	await app.send("setDownloadDirectory", {
		profile,
		path: context.tempDir("pane-lock-downloads-"),
	});

	await page.evaluate(`location.href = ${JSON.stringify(slow)}`);

	await app.waitFor("a download under way", (state) =>
		state.downloads.some((download) => download.state === "inProgress"),
	);

	// Lock Pane: the PIN screen in the command's own reply.
	const asked = Date.now();
	const reply = await app.send("lockNow");
	const shownWith = reply.state.security.screen?.mode;

	const locked = await app.waitFor(
		"the data closed behind the lock",
		(state) =>
			state.security.locked &&
			!state.security.data.attached &&
			state.profiles.length === 0,
		30_000,
	);

	const detached = await poll(
		"the volume detached",
		async () =>
			!(await mounted()) && (await imagesAttachedIn(dataDir)).length === 0,
		30_000,
	).then(
		() => Date.now() - asked,
		() => null,
	);

	const whileLocked = {
		screen: shownWith,
		downloads: (await app.state()).downloads.map((item) => item.state),
		profiles: locked.profiles.length,
		phase: locked.security.data.phase,
		mounted: await mounted(),
		detachedAfterMs: detached,
	};

	expect(
		whileLocked.screen === "unlock" &&
			whileLocked.profiles === 0 &&
			!whileLocked.mounted &&
			whileLocked.detachedAfterMs !== null &&
			whileLocked.detachedAfterMs < 5_000 &&
			!whileLocked.downloads.includes("inProgress"),
		`while locked: ${JSON.stringify(whileLocked)}`,
	);

	// The PIN opens it again: the tab that showed, still signed in.
	await app.send("pinKeys", { keys: "1234" });

	const reopened = await app.waitFor(
		"unlocked again, the tab back",
		(state) =>
			!state.security.locked &&
			state.security.data.attached &&
			state.activeTabID === tab,
		60_000,
	);

	const watchdogs = async () =>
		(await pgrep("-f", `pane-volume-watch ${app.browser.pid} `)).length;

	expect(
		(await watchdogs()) === 1,
		`watchdogs after a lock and an unlock: ${await watchdogs()}`,
	);

	const again = await app.page(tab, url);

	const identity = await again.evaluate<{
		ready: boolean;
		cookie: string;
		storage: string | null;
	}>(
		"(async () => ({ ready: await window.ready, cookie: document.cookie, storage: localStorage.getItem('pane-session') }))()",
	);

	// The page fills its card, drawn at its own size: the window behind the
	// PIN screen changed in the unlock's update, and animated with it, the
	// page made then kept a size from the way, 27 points too tall.
	type Layout = {
		page: { y: number; height: number } | null;
		drawn: { height: number } | null;
	};

	const layout = await poll(
		"the page drawn at its size",
		async () => {
			const { result } = await app.send<Layout>("pageLayout");

			return result.page && result.drawn?.height === result.page.height
				? result
				: null;
		},
		5_000,
	).catch(async () => (await app.send<Layout>("pageLayout")).result);

	const afterUnlock = {
		profiles: reopened.profiles.map((item) => item.name),
		tab: tabOf(reopened, tab)?.url,
		identity,
		layout,
	};

	expect(
		JSON.stringify(afterUnlock.profiles) === '["Work"]' &&
			identity.cookie.includes("pane_session=signed-in") &&
			identity.storage === "kept" &&
			layout.drawn?.height === layout.page?.height,
		`after the unlock: ${JSON.stringify(afterUnlock)}`,
	);

	// Lock after a minute without use: Pane is behind the app in use.
	await app.send("setLockAfter", { minutes: 1 });

	const idle = await app
		.waitFor(
			"locked by the time without use",
			(state) => state.security.locked && state.profiles.length === 0,
			10_000,
		)
		.then(
			() => true,
			() => false,
		);

	expect(idle, "Pane did not lock after a minute without use");

	// The displays sleeping, and another user taking the session, each lock
	// it too, as the system tells Pane alone (its own NSWorkspace center);
	// the screen's lock and the computer's sleep reach every app, the user's
	// own Pane among them, so pane_unittests covers those (LockTriggers).
	await app.send("setLockAfter", { minutes: null });
	const bySystem: Record<string, boolean> = {};

	for (const event of ["screensSleep", "sessionResign"]) {
		await app.send("pinKeys", { keys: "1234" });

		await app.waitFor(
			`unlocked before ${event}`,
			(state) => !state.security.locked && state.security.data.attached,
			60_000,
		);

		await app.send("systemEvent", { event });

		bySystem[event] = await app
			.waitFor(
				`locked by ${event}`,
				(state) => state.security.locked && state.profiles.length === 0,
				10_000,
			)
			.then(
				() => true,
				() => false,
			);
	}

	expect(
		Object.values(bySystem).every(Boolean),
		`locks by the system: ${JSON.stringify(bySystem)}`,
	);

	// A lock the moment the unlock has the volume, as the system's locks above
	// come, while the profile is still opening behind the window: Chrome's
	// shared proto database, which a profile opens as it starts, kept itself
	// open two minutes past a profile that went before it had opened, so the
	// detach gave up after 10 s and Pane quit, a lock in five to ten so timed.
	// Each detaches within the lock's wait, and the PIN that follows, the next
	// round's or the next step's, finds Pane still there: a quit detaches the
	// volume as well.
	const atOpen: (number | null)[] = [];

	for (let round = 0; round < 8 && atOpen.at(-1) !== null; round++) {
		try {
			await app.send("pinKeys", { keys: "1234" });

			await app.waitFor(
				"unlocked, the profile opening",
				(state) => !state.security.locked && state.security.data.attached,
				60_000,
			);
		} catch (error) {
			throw new Error(
				`after the locks as the profile opens, ${JSON.stringify(atOpen)} ms: ${error}`,
			);
		}

		const lockedAt = Date.now();
		await app.send("lockNow");

		atOpen.push(
			await poll(
				"the detach of a lock as the profile opens",
				async () => (await imagesAttachedIn(dataDir)).length === 0,
				15_000,
			).then(
				() => Date.now() - lockedAt,
				() => null,
			),
		);
	}

	writeFileSync(
		join(context.out, "locks-as-the-profile-opens.json"),
		JSON.stringify(atOpen),
	);

	expect(
		atOpen.length === 8 && atOpen.every((ms) => ms !== null),
		`locks as the profile opens, detached after (ms): ${JSON.stringify(atOpen)}`,
	);

	// Chrome posts BEST_EFFORT work that reaches a profile's files as the
	// profile is created (the bounce-tracking database's open) and as it is
	// destroyed (the bookmarks' ordering written), and that work waits while
	// the Mac is busy, or while Chrome's startup holds it: a lock that came as
	// the profile opened dropped the keys before it ran, and the encryption's
	// check of a profile's file with no key ended Pane (ADR 0121). The thread
	// pool holds that work here, 1.5 s from before the PIN, past where each
	// lock detached before; the Lock lets it run while the keys are there, and
	// once it has, Pane is still there.
	const workHeld: (number | null)[] = [];
	const workRan: boolean[] = [];

	for (let round = 0; round < 3; round++) {
		await app.send("holdBackgroundWork", { ms: 1_500 });
		await app.send("pinKeys", { keys: "1234" });

		await app.waitFor(
			"unlocked, the profile opening, its background work held",
			(state) => !state.security.locked && state.security.data.attached,
			60_000,
		);

		const lockedAt = Date.now();
		await app.send("lockNow");

		workHeld.push(
			await poll(
				"the detach of a lock with the background work held",
				async () => (await imagesAttachedIn(dataDir)).length === 0,
				15_000,
			).then(
				() => Date.now() - lockedAt,
				() => null,
			),
		);

		try {
			workRan.push(
				(await app.send<boolean>("waitForBackgroundWork", { ms: 10_000 }))
					.result,
			);
		} catch (error) {
			throw new Error(
				`once the held background work ran, after locks detached at ${JSON.stringify(workHeld)} ms: ${error}`,
			);
		}
	}

	writeFileSync(
		join(context.out, "locks-with-the-background-work-held.json"),
		JSON.stringify({ detachedAfterMs: workHeld, workRan }),
	);

	expect(
		workHeld.every((ms) => ms !== null) && workRan.every(Boolean),
		`locks with the background work held, detached after (ms): ${JSON.stringify(workHeld)}, the work run since: ${JSON.stringify(workRan)}`,
	);

	// Once more through a lock: the image on whichever device it has now.
	await app.send("pinKeys", { keys: "1234" });

	await app.waitFor(
		"unlocked once more",
		(state) => !state.security.locked && state.security.data.attached,
		60_000,
	);

	const watchdogsAtTheEnd = await watchdogs();

	expect(
		watchdogsAtTheEnd === 1,
		`watchdogs after the locks: ${watchdogsAtTheEnd}`,
	);

	// Something else keeps a file of the volume open: a lock never forces the
	// detach, which pulled the files from under the engine, whose next flush stopped
	// it. It goes once the file closes.
	const held = join(mountPoint, "held");
	writeFileSync(held, "held");
	const holder = Bun.spawn(["/usr/bin/tail", "-f", held], { stdout: "ignore" });
	await Bun.sleep(200);
	await app.send("lockNow");
	await Bun.sleep(3_000);
	const heldThrough = (await imagesAttachedIn(dataDir)).length;
	holder.kill();
	await holder.exited;

	const detachedOnceFree = await poll(
		"the lock's detach once the file closed",
		async () => (await imagesAttachedIn(dataDir)).length === 0,
		10_000,
	).then(
		() => true,
		() => false,
	);

	expect(
		heldThrough === 1 && detachedOnceFree,
		`a file held through a lock: attached ${heldThrough}, then detached ${detachedOnceFree}`,
	);

	await app.send("pinKeys", { keys: "1234" });

	await app.waitFor(
		"unlocked after the wait",
		(state) => !state.security.locked && state.security.data.attached,
		60_000,
	);

	process.kill(app.browser.pid, "SIGKILL");
	await waitForExit(app.browser.pid);

	const detachedAfterKill = await poll(
		"the watchdog's detach after a kill",
		async () => (await imagesAttachedIn(dataDir)).length === 0,
		10_000,
	).then(
		() => true,
		() => false,
	);

	expect(detachedAfterKill, "a kill after two locks left the image attached");

	// A launch's first lock, the moment its first unlock attaches, before any
	// page has loaded: Chrome holds every BEST_EFFORT task until its startup
	// is over, once a visible page has loaded, else 3 minutes after launch,
	// and the profile's work stayed held behind the lock, to run when the 3
	// minutes ran out, its key gone. The lock ends the startup and lets that
	// work run first: once the volume has detached, the thread pool holds
	// nothing.
	const last = await launchPane(context, { dataDir });
	await last.send("pinKeys", { keys: "1234" });

	await last.waitFor(
		"unlocked, the launch's first",
		(state) => !state.security.locked && state.security.data.attached,
		60_000,
	);

	const firstLockedAt = Date.now();
	await last.send("lockNow");

	const firstDetached = await poll(
		"the detach of the launch's first lock",
		async () => (await imagesAttachedIn(dataDir)).length === 0,
		15_000,
	).then(
		() => Date.now() - firstLockedAt,
		() => null,
	);

	const { result: poolEmptied } = await last.send<boolean>(
		"waitForBackgroundWork",
		{ ms: 10_000 },
	);

	expect(
		firstDetached !== null && poolEmptied,
		`the launch's first lock, before a page loaded: detached after ${firstDetached} ms, the thread pool emptied ${poolEmptied}`,
	);

	// Held past the wait: Pane quits, and the volume detaches after the engine.
	await last.send("pinKeys", { keys: "1234" });

	await last.waitFor(
		"unlocked for the long hold",
		(state) => !state.security.locked && state.security.data.attached,
		60_000,
	);

	const longHolder = Bun.spawn(["/usr/bin/tail", "-f", held], {
		stdout: "ignore",
	});

	await Bun.sleep(200);
	await last.send("lockNow");
	const quitOnItsOwn = await waitForExit(last.browser.pid, 30_000);
	longHolder.kill();
	await longHolder.exited;

	const detachedAfterQuit = await poll(
		"the quit's detach",
		async () => (await imagesAttachedIn(dataDir)).length === 0,
		15_000,
	).then(
		() => true,
		() => false,
	);

	expect(
		quitOnItsOwn && detachedAfterQuit,
		`a file held past the wait: quit ${quitOnItsOwn}, detached ${detachedAfterQuit}`,
	);
};

export default lockWhileOpen;
