import { writeFileSync } from "node:fs";
import { join } from "node:path";
import { launchPane, setUpPin, tabOf } from "../lib/pane";
import { poll } from "../lib/poll";
import { imagesAttachedIn, waitForExit } from "../lib/processes";
import { expect, type Scenario } from "../run";

// Pane locks while open (ADR 0116, Paper 06.4 and 07.9). Lock Pane shows the
// PIN screen at once; behind it the profiles leave memory, each closes in
// the engine, and the volume detaches. The PIN opens them again, and the tab
// that showed comes back, still signed in. A download under way holds its
// profile open in Chrome: the lock stops it, as a quit does, and the volume
// still detaches whole, never forced from under the engine. With Lock after
// set, Pane locks by itself once it has gone that long without use (a minute
// a second here). One watchdog serves a Pane through every lock, and still
// detaches the image when Pane is killed. A file something else holds open is never
// pulled away: the lock waits, and past 10 s Pane quits.

const lockWhileOpen: Scenario = async (context) => {
	const dataDir = context.tempDir("pane-lock-");
	const mountPoint = join(dataDir, "Engine", "Volume");

	const mounted = async () => {
		const table = await Bun.$`/sbin/mount`.text();

		return (
			table.includes(` on ${mountPoint} `) ||
			table.includes(` on /private${mountPoint} `)
		);
	};

	const app = await launchPane(context, {
		dataDir,
		env: { PANE_TIME_SCALE: "60" },
	});

	await app.send("setLockAfter", { minutes: null });
	await setUpPin(app, "1234");

	const profile = await app.createProfile("Work", "BLUE");
	const url = context.fixtures.url("sign-in");
	const tab = await app.openTab(profile, url);
	const page = await app.page(tab, url);
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
		(
			await Bun.$`/usr/bin/pgrep -f ${`pane-volume-watch ${app.browser.pid} `}`
				.quiet()
				.nothrow()
				.text()
		)
			.split("\n")
			.filter(Boolean).length;

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

	// Once more through a lock: the image on whichever device it has now.
	await app.send("setLockAfter", { minutes: null });
	await app.send("pinKeys", { keys: "1234" });

	await app.waitFor(
		"unlocked once more",
		(state) => !state.security.locked && state.security.data.attached,
		60_000,
	);

	const watchdogsAtTheEnd = await watchdogs();

	expect(
		watchdogsAtTheEnd === 1,
		`watchdogs after two locks: ${watchdogsAtTheEnd}`,
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

	// Held past the wait: Pane quits, and the volume detaches after the engine.
	const last = await launchPane(context, { dataDir });
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
