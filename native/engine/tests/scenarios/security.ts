import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { type AppState, launchPane, type PaneApp } from "../lib/pane";
import { waitForExit } from "../lib/processes";
import { expect, type Scenario } from "../run";

// The PIN lock. Pane starts locked behind a PIN the Electron app set
// (security.json with its bcryptjs hash of 1234): behind it nothing opens,
// neither the URL Pane was started with nor the menu bar's items (the
// mouse's way past the PIN screen's keys) but Quit and Check for Updates….
// A wrong PIN counts, the right one unlocks, and the URL opens then. Then
// the PIN is changed, removed, and set up again through the PIN screen, and
// Back leaves it. On a relaunch, five wrong PINs erase the data directory
// and Pane starts again, fresh. Last, ⌘Q quits from the PIN screen with
// Caps Lock on. Each check runs in a throwaway data directory.

// bcryptjs 3.0.3, hashSync("1234", 10): what the Electron app wrote.
const ELECTRON_SECURITY = {
	state: {
		pin: {
			hash: "$2b$10$6RNvGASNquZ6f5LXjFap1eMcHOZTOzRYJVRe1DrjRG2juL90W5MNO",
			length: 4,
		},
		failedAttempts: 0,
	},
	version: 0,
};

type Saved = {
	state: {
		pin: { hash: string; length: number } | null;
		failedAttempts: number;
	};
};

function saved(dataDir: string): Saved | null {
	const file = join(dataDir, "security.json");

	return existsSync(file)
		? (JSON.parse(readFileSync(file, "utf8")) as Saved)
		: null;
}

function screen(state: AppState) {
	return state.security.screen;
}

// Types `keys`, then waits until the PIN screen has taken them (a check or a
// shake runs as `busy`).
async function type(
	app: PaneApp,
	keys: string,
	description: string,
	until: (state: AppState) => boolean,
) {
	await app.send("pinKeys", { keys });

	return app.waitFor(description, until, 10_000);
}

const security: Scenario = async (context) => {
	const dataDir = context.tempDir("pane-security-");
	mkdirSync(dataDir, { recursive: true });

	writeFileSync(
		join(dataDir, "security.json"),
		JSON.stringify(ELECTRON_SECURITY),
	);

	const launchURL = context.fixtures.url("nav-b.html?behind-the-lock");

	const app = await launchPane(context, { dataDir, args: [launchURL] });
	const launched = await app.state();

	expect(
		launched.security.locked &&
			screen(launched)?.mode === "unlock" &&
			screen(launched)?.dots === 4,
		`Pane did not start locked: ${JSON.stringify(launched.security)}`,
	);

	// The menu bar, as clicked: an item runs only if the menu enables it.
	const menu = async (path: string[]) =>
		(await app.send<boolean>("menu", { path })).result;

	const lockedMenu = {
		newTab: await menu(["File", "New Tab"]),
		reopenClosedTab: await menu(["File", "Reopen Closed Tab"]),
		print: await menu(["File", "Print\u2026"]),
		developerTools: await menu(["View", "Developer Tools"]),
		tab1: await menu(["Tab", "Tab 1"]),
		settings: await menu(["Pane", "Settings\u2026"]),
		extensions: await menu(["Window", "Extensions"]),
		checkForUpdates: await menu(["Pane", "Check for Updates\u2026"]),
	};

	// Long enough for a tab to open and load, had one been let through.
	await Bun.sleep(1_500);
	const behindLock = await app.state();
	const tabsBehindLock = behindLock.profiles.flatMap((entry) => entry.tabs);

	expect(
		Object.entries(lockedMenu).every(
			([item, ran]) => ran === (item === "checkForUpdates"),
		),
		`the menu behind the lock: ${JSON.stringify(lockedMenu)}`,
	);

	expect(
		tabsBehindLock.length === 0 &&
			behindLock.window.title === "Pane" &&
			behindLock.page === "browser" &&
			behindLock.sheet === null,
		`something opened behind the lock: ${JSON.stringify({ tabs: tabsBehindLock, title: behindLock.window.title, page: behindLock.page, sheet: behindLock.sheet })}`,
	);

	await app.send("render", { path: join(context.out, "security-lock.png") });

	// A wrong PIN.
	const wrong = await type(
		app,
		"0000",
		"the wrong PIN counted",
		(state) =>
			state.security.failedAttempts === 1 &&
			screen(state)?.entered === 0 &&
			!screen(state)?.busy,
	);

	expect(wrong.security.locked, "a wrong PIN unlocked Pane");

	expect(
		saved(dataDir)?.state.failedAttempts === 1,
		"the failed attempt is not on disk",
	);

	await Bun.sleep(500);

	await app.send("render", {
		path: join(context.out, "security-wrong-pin.png"),
	});

	// The Electron app's PIN.
	const unlocked = await type(
		app,
		"1234",
		"unlocked",
		(state) => !state.security.locked && screen(state) === null,
	);

	expect(unlocked.security.failedAttempts === 0, "the attempts did not reset");

	// The launch URL opens once the lock is open.
	const opened = await app.waitFor("the launch URL opened", (state) =>
		state.profiles.some((entry) =>
			entry.tabs.some((item) => item.url === launchURL && item.loaded),
		),
	);

	// Data that the wipe must erase later: a profile with a page.
	const profile = await app.createProfile("Private", "ROSE");
	const page = context.fixtures.url("nav-a.html?private");
	const tab = await app.openTab(profile, page);
	await app.page(tab, page);

	// Change: the current PIN, then a new one twice.
	await app.send("showPinScreen", { mode: "change" });

	await type(
		app,
		"1234",
		"the new PIN asked",
		(state) => screen(state)?.step === "enter",
	);

	await type(
		app,
		"567890>",
		"the new PIN confirmed",
		(state) => screen(state)?.step === "confirm",
	);

	const changed = await type(
		app,
		"567890",
		"the PIN changed",
		(state) => screen(state) === null && state.security.pinLength === 6,
	);

	const changedHash = saved(dataDir)?.state.pin?.hash ?? "";

	expect(
		changedHash.startsWith("$2b$10$") &&
			changedHash !== ELECTRON_SECURITY.state.pin.hash,
		`the new hash: ${changedHash}`,
	);

	// Remove: the current PIN.
	await app.send("showPinScreen", { mode: "remove" });

	const removed = await type(
		app,
		"567890",
		"the PIN removed",
		(state) => screen(state) === null && state.security.pinLength === null,
	);

	expect(saved(dataDir)?.state.pin === null, "security.json still holds a PIN");

	// Set up: two PINs that differ, then the same twice.
	await app.send("showPinScreen", { mode: "setup" });

	await type(
		app,
		"2468>",
		"confirming",
		(state) => screen(state)?.step === "confirm",
	);

	const mismatch = await type(
		app,
		"2469",
		"the mismatch",
		(state) => screen(state)?.error === "PINs don't match",
	);

	await app.waitFor(
		"the mismatch cleared",
		(state) => screen(state)?.entered === 0 && !screen(state)?.busy,
	);

	const setUp = await type(
		app,
		"2468",
		"the PIN set up",
		(state) => screen(state) === null && state.security.pinLength === 4,
	);

	// Back leaves the screen without changing anything.
	await app.send("showPinScreen", { mode: "change" });

	const back = await type(
		app,
		"^",
		"the screen left",
		(state) => screen(state) === null,
	);

	expect(
		back.security.pinLength === 4 && !back.security.locked,
		"Back changed the lock",
	);

	// Relaunch: locked again, then the panic wipe.
	await app.browser.close();

	const relaunched = await launchPane(context, { dataDir });
	const locked = await relaunched.state();

	expect(
		locked.security.locked && screen(locked)?.dots === 4,
		"Pane did not start locked with the new PIN",
	);

	for (let attempt = 1; attempt <= 4; attempt++) {
		await type(
			relaunched,
			"1111",
			`wrong PIN ${attempt}`,
			(state) =>
				state.security.failedAttempts === attempt &&
				screen(state)?.entered === 0 &&
				!screen(state)?.busy,
		);
	}

	// Past the dots' fade.
	await Bun.sleep(500);

	await relaunched.send("render", {
		path: join(context.out, "security-last-attempt.png"),
	});

	const profileDir = join(dataDir, "Engine", profile);

	const before = {
		profiles: existsSync(join(dataDir, "profiles.json")),
		profileData: existsSync(profileDir),
	};

	// The wiped instance's helpers (renderers, GPU, network service), which
	// must not outlive it to write after the erase.
	const helpers = (
		await Bun.$`pgrep -P ${relaunched.browser.pid}`.quiet().nothrow().text()
	)
		.split("\n")
		.filter(Boolean)
		.map(Number);

	// The fifth: Pane erases its data and starts again. The new instance is
	// no child of the runner, which stops it with whatever else still uses
	// the scenario's directories, however the scenario ends.
	await relaunched.send("pinKeys", { keys: "1111" });

	expect(
		await waitForExit(relaunched.browser.pid, 30_000),
		"Pane still runs after the fifth wrong PIN",
	);

	// The new instance answers the same automation channel; it starts on
	// nothing at once and erases the moved data on a thread.
	const fresh = await relaunched.waitFor(
		"Pane started again",
		(state) => !state.profiles.some((entry) => entry.id === profile),
		30_000,
	);

	const erased = await relaunched.waitFor(
		"the wipe's erase done",
		(state) => !state.security.wipe.erasing,
		30_000,
	);

	const helpersAlive = [];

	for (const pid of helpers) {
		if ((await Bun.$`kill -0 ${pid}`.quiet().nothrow()).exitCode === 0) {
			helpersAlive.push(pid);
		}
	}

	// Nothing comes back a moment later: no helper flushed after the erase.
	await Bun.sleep(3_000);

	const after = {
		profileData: existsSync(profileDir),
		staged: existsSync(join(dataDir, "Wiping")),
		failures: erased.security.wipe.failures,
		helpers: { before: helpers.length, alive: helpersAlive },
		security: saved(dataDir),
		locked: fresh.security.locked,
		pinLength: fresh.security.pinLength,
		profiles: fresh.profiles.length,
	};

	await relaunched.quit();

	// ⌘Q at the PIN screen with Caps Lock on.
	const capsDir = context.tempDir("pane-security-caps-");

	writeFileSync(
		join(capsDir, "security.json"),
		JSON.stringify(ELECTRON_SECURITY),
	);

	const caps = await launchPane(context, { dataDir: capsDir });
	await caps.send("activate");
	await caps.waitFor("the key window", (state) => state.window.key, 10_000);
	await caps.send("key", { key: "q", modifiers: ["command", "capsLock"] });

	const quitWithCapsLock = await waitForExit(caps.browser.pid);

	if (!quitWithCapsLock) {
		await caps.browser.close();
	}

	expect(
		before.profiles && before.profileData,
		`nothing to wipe: ${JSON.stringify(before)}`,
	);

	expect(!after.profileData, "the profile's engine data survived the wipe");

	expect(
		!after.staged && after.failures.length === 0,
		`the wipe left data: ${JSON.stringify({ staged: after.staged, failures: after.failures })}`,
	);

	expect(
		after.helpers.before > 0 && after.helpers.alive.length === 0,
		`the wiped instance's helpers outlived it: ${JSON.stringify(after.helpers)}`,
	);

	expect(quitWithCapsLock, "⌘Q with Caps Lock did not quit at the PIN screen");

	expect(
		!after.locked &&
			after.pinLength === null &&
			(after.security?.state.pin ?? null) === null,
		`the PIN survived: ${JSON.stringify(after)}`,
	);

	return {
		behindTheLock: {
			menu: lockedMenu,
			tabs: tabsBehindLock.length,
			windowTitle: behindLock.window.title,
			launchURLAfterUnlock: opened.profiles.some((entry) =>
				entry.tabs.some((item) => item.url === launchURL),
			),
		},
		quitWithCapsLock,
		electronPin: {
			unlockedWith: "the Electron app's bcryptjs hash",
			failedAttemptsAfterWrong: wrong.security.failedAttempts,
		},
		change: {
			pinLength: changed.security.pinLength,
			hashPrefix: changedHash.slice(0, 7),
		},
		remove: { pinLength: removed.security.pinLength },
		setup: {
			mismatch: screen(mismatch)?.error,
			pinLength: setUp.security.pinLength,
		},
		back: { pinLength: back.security.pinLength },
		wipe: { before, after },
	};
};

export default security;
