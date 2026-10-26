import {
	existsSync,
	mkdirSync,
	mkdtempSync,
	readFileSync,
	writeFileSync,
} from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { type AppState, launchPane, type PaneApp } from "../lib/pane";
import { expect, type Scenario } from "../run";

// The PIN lock. Pane starts locked behind a PIN the Electron app set
// (security.json with its bcryptjs hash of 1234): a wrong PIN counts, the
// right one unlocks. Then the PIN is changed, removed, and set up again
// through the PIN screen, and Back leaves it. Last, on a relaunch, five
// wrong PINs erase the data directory and Pane starts again, fresh. Each
// check runs in a throwaway data directory.

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
	const dataDir = mkdtempSync(join(tmpdir(), "pane-security-"));
	mkdirSync(dataDir, { recursive: true });
	writeFileSync(
		join(dataDir, "security.json"),
		JSON.stringify(ELECTRON_SECURITY),
	);

	const app = await launchPane(context, { dataDir });
	const launched = await app.state();

	expect(
		launched.security.locked &&
			screen(launched)?.mode === "unlock" &&
			screen(launched)?.dots === 4,
		`Pane did not start locked: ${JSON.stringify(launched.security)}`,
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

	// The fifth: Pane erases its data and starts again.
	await relaunched.send("pinKeys", { keys: "1111" });

	const deadline = Date.now() + 30_000;

	while (
		Date.now() < deadline &&
		(await Bun.$`kill -0 ${relaunched.browser.pid}`.quiet().nothrow())
			.exitCode === 0
	) {
		await Bun.sleep(200);
	}

	// The new instance answers the same automation channel.
	const fresh = await relaunched.waitFor(
		"Pane started again",
		(state) => !state.profiles.some((entry) => entry.id === profile),
		30_000,
	);

	const after = {
		profileData: existsSync(profileDir),
		security: saved(dataDir),
		locked: fresh.security.locked,
		pinLength: fresh.security.pinLength,
		profiles: fresh.profiles.length,
	};

	await relaunched.send("quit").catch(() => {});
	await Bun.sleep(2_000);
	await Bun.$`pkill -f ${join(dataDir, "Engine")}`.quiet().nothrow();

	expect(
		before.profiles && before.profileData,
		`nothing to wipe: ${JSON.stringify(before)}`,
	);
	expect(!after.profileData, "the profile's engine data survived the wipe");
	expect(
		!after.locked &&
			after.pinLength === null &&
			(after.security?.state.pin ?? null) === null,
		`the PIN survived: ${JSON.stringify(after)}`,
	);

	return {
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
