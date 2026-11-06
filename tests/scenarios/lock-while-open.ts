import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { launchPane, tabOf } from "../lib/pane";
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
// detaches the image when Pane is killed.

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
	await app.send("pinKeys", { keys: "1234" });

	await app.waitFor(
		"unlocked, the volume attached",
		(state) => !state.security.locked && state.security.data.attached,
		60_000,
	);

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

	return { whileLocked, afterUnlock, idleLock: idle };
};

export default lockWhileOpen;
