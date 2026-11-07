import { existsSync, lstatSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import {
	type AppState,
	bringToFront,
	launchPane,
	type PaneApp,
	setUpPin,
} from "../lib/pane";
import { poll } from "../lib/poll";
import { imagesAttachedIn, waitForExit } from "../lib/processes";
import { swiftTool } from "../lib/tools";
import { expect, type Scenario, type ScenarioContext } from "../run";

// The PIN lock. Pane starts locked behind a PIN set up through the PIN screen:
// behind it nothing opens, neither the URL Pane was started with, nor one another app hands it, nor the
// menu bar's items (the mouse's way past the PIN screen's keys) but Quit and
// Check for Updates…. A wrong PIN counts, the right one unlocks, and the URLs
// open then. Then the PIN is changed, removed, and set up again through the PIN
// screen, and Back leaves it. On a relaunch, five wrong PINs erase the data
// directory and Pane starts again, fresh. Then ⌘Q quits from the PIN screen
// with Caps Lock on. Last, a security.json that no longer decodes never opens
// the lock: the key's copy beside the volume still does, with the PIN, and with
// none the lock stays shut. Each check runs in a throwaway data directory.
//
// A PIN from before the data key, no more than a bcrypt hash and no key, takes no
// keys, counts no attempts, and makes no key: it stops at 07.6 (ADR 0122).
//
// Wrong PINs in Settings erase nothing (ADR 0042): they count in memory, and the
// fifth only locks Pane. A last attempt on disk with the key still there, which
// Settings left in 0.18.0, erases nothing either, and leaves the lock its attempts.
//
// The PIN opens the data key (ADR 0113), sealed with it, and a change or a
// removal seals it again. Behind the lock the data volume is locked and
// mounted nowhere, each profile's link leads nowhere, and nothing a page stored, nor its address,
// is in any file in the clear. The panic wipe destroys the key and erases the
// volume: the fresh Pane seals a key of its own.
// A quit detaches the volume, and so does its watchdog when Pane is killed.

// bcryptjs 3.0.3, hashSync("1234", 10): what a Pane before 0.14 kept.
const BCRYPT_ONLY = {
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
		pin: { length: number } | null;
		failedAttempts: number;
		key?: {
			box: string;
			pin?: { salt: string; rounds: number } | null;
			secret: string;
		};
	};
};

// security.json, or its copy beside the volume ("Data.security").
function saved(dataDir: string, name = "security"): Saved | null {
	const file = join(dataDir, `${name}.json`);

	return existsSync(file)
		? (JSON.parse(readFileSync(file, "utf8")) as Saved)
		: null;
}

// A link, whether or not it leads anywhere.
function lexists(path: string): boolean {
	try {
		lstatSync(path);

		return true;
	} catch {
		return false;
	}
}

// Files under `root` whose bytes hold `text` in the clear, the automation
// channel's own files aside.
async function holding(root: string, text: string): Promise<string[]> {
	return (await Bun.$`grep -rlaF ${text} ${root}`.quiet().nothrow().text())
		.split("\n")
		.filter((path) => path !== "" && !path.includes("/automation/"));
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

// The count on disk, in security.json and its copy, with the key left in both.
function setFailedAttempts(dataDir: string, failedAttempts: number) {
	for (const name of ["security", "Data.security"]) {
		const file = saved(dataDir, name);

		expect(file !== null, `no ${name}.json`);

		writeFileSync(
			join(dataDir, `${name}.json`),
			JSON.stringify({
				...file,
				state: { ...file.state, failedAttempts },
			}),
		);
	}
}

// A data folder with a PIN, set up through the PIN screen, and Pane quit.
async function withPin(context: ScenarioContext, dataDir: string, pin: string) {
	const app = await launchPane(context, { dataDir });
	await setUpPin(app, pin);
	await app.browser.close();
}

// A PIN from before the data key, with no key: nothing left checks it, so no PIN
// opens it, no attempt counts, since the fifth erases, and no key is made.
async function pinWithNoKey(context: ScenarioContext) {
	const dataDir = context.tempDir("pane-security-no-key-");
	const contents = JSON.stringify(BCRYPT_ONLY);
	writeFileSync(join(dataDir, "security.json"), contents);

	const app = await launchPane(context, { dataDir });
	const atLaunch = await app.state();

	await app.send("pinKeys", { keys: "1234" });
	await Bun.sleep(1_000);
	await app.send("pinKeys", { keys: "0000" });
	await Bun.sleep(1_000);

	const afterKeys = await app.state();
	await app.browser.close();

	const result = {
		error: screen(atLaunch)?.error ?? null,
		locked: afterKeys.security.locked,
		failedAttempts: afterKeys.security.failedAttempts,
		file: readFileSync(join(dataDir, "security.json"), "utf8") === contents,
		keyCopy: existsSync(join(dataDir, "Data.security.json")),
		image: existsSync(join(dataDir, "Data.sparsebundle")),
	};

	expect(
		result.error ===
			"Pane can\u{2019}t read the key to your data.\nQuit, then restore security.json." &&
			result.locked &&
			result.failedAttempts === 0 &&
			result.file &&
			!result.keyCopy &&
			!result.image,
		`a PIN with no key: ${JSON.stringify(result)}`,
	);

	return result;
}

// A profile's page, reading back what it stored once the PIN opened it.
async function settingsAttempts(context: ScenarioContext) {
	const dataDir = context.tempDir("pane-security-settings-");
	const PIN = "2468";
	const first = await launchPane(context, { dataDir });
	const profile = await first.createProfile("Kept", "BLUE");
	const url = context.fixtures.url("nav-a.html?kept");
	const tab = await first.openTab(profile, url);
	const value = `kept-${crypto.randomUUID()}`;

	await (await first.page(tab, url)).evaluate(
		`localStorage.setItem("kept", ${JSON.stringify(value)})`,
	);

	await setUpPin(first, PIN);
	await first.browser.close();

	// The PIN, then the page's value: the data is there.
	const unlocks = async (app: PaneApp, when: string) => {
		await type(
			app,
			PIN,
			`the PIN opening Pane ${when}`,
			(state) =>
				!state.security.locked &&
				state.security.data.attached &&
				state.profiles.some((entry) => entry.id === profile),
		);

		await app.send("select", { tab });

		const read = await (await app.page(tab, url)).evaluate<string | null>(
			`localStorage.getItem("kept")`,
		);

		expect(read === value, `the data ${when}: ${read}`);
	};

	const wrongInChange = async (app: PaneApp, times: number) => {
		await app.send("showPinScreen", { mode: "change" });

		for (let attempt = 1; attempt <= times; attempt++) {
			await type(
				app,
				"1111",
				`wrong PIN ${attempt} in Change`,
				(state) => screen(state)?.entered === 0 && !screen(state)?.busy,
			);
		}
	};

	// What Settings left on disk in 0.18.0: the last attempt, with the key. A
	// wipe had stood due at this launch.
	setFailedAttempts(dataDir, 5);
	const leftBehind = await launchPane(context, { dataDir });
	const atLaunch = await leftBehind.state();

	expect(
		atLaunch.security.locked && !atLaunch.security.wipe.erasing,
		`five attempts on disk, with the key: ${JSON.stringify(atLaunch.security)}`,
	);

	expect(
		lexists(join(dataDir, "Engine", profile)),
		"five attempts on disk, with the key, erased the data",
	);

	// A typo there is one attempt of five.
	const typo = await type(
		leftBehind,
		"1111",
		"a wrong PIN after them",
		(state) =>
			state.security.failedAttempts > 0 &&
			screen(state)?.entered === 0 &&
			!screen(state)?.busy,
	);

	expect(
		typo.security.failedAttempts === 1 &&
			(await waitForExit(leftBehind.browser.pid, 500)) === false,
		`a wrong PIN after them: ${JSON.stringify(typo.security)}`,
	);

	await unlocks(leftBehind, "after five attempts on disk");

	// Four in Change, then a quit: nothing counted, and the data is there.
	await wrongInChange(leftBehind, 4);

	expect(
		saved(dataDir)?.state.failedAttempts === 0,
		`four wrong in Change, on disk: ${saved(dataDir)?.state.failedAttempts}`,
	);

	await leftBehind.browser.close();
	const afterFour = await launchPane(context, { dataDir });
	await unlocks(afterFour, "after four wrong in Change");

	// Five in Change: Pane locks, erasing nothing, and the right PIN opens it.
	await wrongInChange(afterFour, 5);

	const lockedOut = await afterFour.waitFor(
		"Pane locked by the fifth wrong PIN in Change",
		(state) => state.security.locked && screen(state)?.mode === "unlock",
		5_000,
	);

	expect(
		saved(dataDir)?.state.failedAttempts === 0 &&
			lockedOut.security.failedAttempts === 0,
		`five wrong in Change, on disk: ${saved(dataDir)?.state.failedAttempts}`,
	);

	await unlocks(afterFour, "after five wrong in Change");
	await afterFour.browser.close();
}

const security: Scenario = async (context) => {
	await pinWithNoKey(context);
	await settingsAttempts(context);

	const dataDir = context.tempDir("pane-security-");
	await withPin(context, dataDir, "1234");

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

	// A link clicked in another app, as Launch Services hands it over.
	const handedURL = context.fixtures.url("nav-a.html?handed-behind-the-lock");

	const handed =
		await Bun.$`${await swiftTool("open-urls")} ${dirname(dirname(dirname(context.binary)))} ${app.browser.pid} ${context.tempDir("pane-security-home-")} ${handedURL}`
			.quiet()
			.nothrow();

	expect(
		handed.exitCode === 0,
		`open-urls failed: ${handed.stderr.toString().trim()}`,
	);

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

	const unlocked = await type(
		app,
		"1234",
		"unlocked",
		(state) => !state.security.locked && screen(state) === null,
	);

	expect(unlocked.security.failedAttempts === 0, "the attempts did not reset");

	// The data key is sealed with the PIN, in security.json and its copy beside
	// the volume.
	const sealedAtUnlock = saved(dataDir)?.state;

	expect(
		sealedAtUnlock?.pin?.length === 4 &&
			!!sealedAtUnlock.key?.pin?.salt &&
			saved(dataDir, "Data.security")?.state.key?.box ===
				sealedAtUnlock.key.box,
		`the data key at the first unlock: ${JSON.stringify(sealedAtUnlock)}`,
	);

	// The URLs open once the lock is open, the handed one last.
	await app.waitFor("the launch and handed URLs opened", (state) =>
		state.profiles.some((entry) => {
			const urls = entry.tabs.map((item) => item.url);
			const handedTab = entry.tabs.find((item) => item.url === handedURL);

			return (
				urls.indexOf(launchURL) >= 0 &&
				urls.indexOf(handedURL) > urls.indexOf(launchURL) &&
				handedTab?.loaded === true
			);
		}),
	);

	// Data that the wipe must erase later: a profile with a page, which
	// stores a marker in a cookie and local storage.
	const profile = await app.createProfile("Private", "ROSE");
	const page = context.fixtures.url("nav-a.html?private");
	const tab = await app.openTab(profile, page);
	const marker = `pane-private-${crypto.randomUUID()}`;

	await (await app.page(tab, page)).evaluate(
		`document.cookie = "private=${marker}; max-age=3600"; localStorage.setItem("private", "${marker}")`,
	);

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

	await type(
		app,
		"567890",
		"the PIN changed",
		(state) => screen(state) === null && state.security.pinLength === 6,
	);

	// Sealed again with the new PIN: a salt of its own.
	const sealedAtChange = saved(dataDir)?.state;

	expect(
		sealedAtChange?.pin?.length === 6 &&
			!!sealedAtChange.key?.pin?.salt &&
			sealedAtChange.key.pin.salt !== sealedAtUnlock?.key?.pin?.salt,
		`the key at the change: ${JSON.stringify(sealedAtChange)}`,
	);

	// Remove: the current PIN.
	await app.send("showPinScreen", { mode: "remove" });

	await type(
		app,
		"567890",
		"the PIN removed",
		(state) => screen(state) === null && state.security.pinLength === null,
	);

	expect(
		(saved(dataDir)?.state.pin ?? null) === null &&
			!!saved(dataDir)?.state.key &&
			(saved(dataDir)?.state.key?.pin ?? null) === null,
		`security.json after the removal: ${JSON.stringify(saved(dataDir))}`,
	);

	// Set up: two PINs that differ, then the same twice.
	await app.send("showPinScreen", { mode: "setup" });

	await type(
		app,
		"2468>",
		"confirming",
		(state) => screen(state)?.step === "confirm",
	);

	await type(
		app,
		"2469",
		"the mismatch",
		(state) => screen(state)?.error === "PINs don't match",
	);

	await app.waitFor(
		"the mismatch cleared",
		(state) => screen(state)?.entered === 0 && !screen(state)?.busy,
	);

	await type(
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

	// Behind the lock: the image attached, its volume locked and mounted
	// nowhere, the profile's link leading nowhere, and the page's marker and
	// address in no file in the clear.
	const mountPoint = join(dataDir, "Engine", "Volume");

	const lockedData = {
		attached: locked.security.data.attached,
		mounted:
			(await Bun.$`mount`.text()).includes(` on ${mountPoint} `) ||
			(await Bun.$`mount`.text()).includes(` on /private${mountPoint} `),
		link: lexists(join(dataDir, "Engine", profile)),
		leadsNowhere: !existsSync(join(dataDir, "Engine", profile)),
		marker: await holding(dataDir, marker),
		address: await holding(dataDir, "nav-a.html?private"),
	};

	expect(
		!lockedData.attached &&
			!lockedData.mounted &&
			lockedData.link &&
			lockedData.leadsNowhere &&
			lockedData.marker.length === 0 &&
			lockedData.address.length === 0,
		`behind the lock: ${JSON.stringify(lockedData)}`,
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

	// A migration's record, from a Pane that came from the Electron app, which
	// names each profile it brought.
	writeFileSync(join(dataDir, "migration.json"), "{}");

	const before = {
		profiles: existsSync(join(dataDir, "profiles.json")),
		profileData: lexists(profileDir),
		volume: existsSync(join(dataDir, "Data.sparsebundle")),
		key: saved(dataDir)?.state.key?.box ?? null,
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
		profileData: lexists(profileDir),
		// The fresh Pane's own volume, attached: the wiped profile is not in it.
		profileInVolume: existsSync(join(dataDir, "Engine", "Volume", profile)),
		migrationRecord: existsSync(join(dataDir, "migration.json")),
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
	await withPin(context, capsDir, "1234");

	const caps = await launchPane(context, { dataDir: capsDir });
	let quitWithCapsLock: boolean | null = null;

	if (await bringToFront(context, caps, "⌘Q with Caps Lock on")) {
		await caps.send("key", { key: "q", modifiers: ["command", "capsLock"] });
		quitWithCapsLock = await waitForExit(caps.browser.pid);
	}

	if (!quitWithCapsLock) {
		await caps.browser.close();
	}

	// A quit detaches the volume; so does the watchdog when Pane is killed.
	const detachDir = context.tempDir("pane-security-detach-");
	const attachedApp = await launchPane(context, { dataDir: detachDir });
	const detachProfile = await attachedApp.createProfile("Work", "BLUE");
	const detachPage = context.fixtures.url("nav-b.html?detach");

	await attachedApp.page(
		await attachedApp.openTab(detachProfile, detachPage),
		detachPage,
	);

	const whileOpen = (await imagesAttachedIn(detachDir)).length;
	await attachedApp.browser.close();

	// The detach ends a moment after the process does.
	const afterQuit = await poll(
		"the volume to detach after a quit",
		async () => {
			const attached = (await imagesAttachedIn(detachDir)).length;

			return attached === 0 ? "none" : false;
		},
		15_000,
		200,
	)
		.then(() => 0)
		.catch(async () => (await imagesAttachedIn(detachDir)).length);

	const killedApp = await launchPane(context, { dataDir: detachDir });

	await killedApp.waitFor(
		"the volume attached",
		(state) => state.security.data.attached,
	);

	const whileOpenAgain = (await imagesAttachedIn(detachDir)).length;

	// The watchdog is in a session of its own, apart from Pane's process
	// group: a kill of that group had ended both and left the volume mounted.
	const groupOf = async (pid: string) =>
		(await Bun.$`/bin/ps -o pgid= -p ${pid}`.quiet().nothrow().text()).trim();

	const watchdog =
		(
			await Bun.$`/usr/bin/pgrep -f ${`pane-volume-watch ${killedApp.browser.pid} `}`
				.quiet()
				.nothrow()
				.text()
		)
			.trim()
			.split("\n")[0] ?? "";

	const watchdogGroup = watchdog ? await groupOf(watchdog) : "";
	const paneGroup = await groupOf(String(killedApp.browser.pid));
	const watchdogApart = watchdogGroup !== "" && watchdogGroup !== paneGroup;

	process.kill(killedApp.browser.pid, "SIGKILL");
	await waitForExit(killedApp.browser.pid);

	let afterKill = whileOpenAgain;

	for (
		const until = Date.now() + 3_000;
		afterKill > 0 && Date.now() < until;
		await Bun.sleep(100)
	) {
		afterKill = (await imagesAttachedIn(detachDir)).length;
	}

	const detached = {
		whileOpen,
		afterQuit,
		whileOpenAgain,
		watchdog: { pid: watchdog, group: watchdogGroup, paneGroup },
		afterKill,
	};

	expect(
		whileOpen === 1 &&
			afterQuit === 0 &&
			whileOpenAgain === 1 &&
			watchdogApart &&
			afterKill === 0,
		`the volume after a quit and a kill: ${JSON.stringify(detached)}`,
	);

	// A damaged security.json: cut short with its key's copy beside it, then
	// garbage with none.
	const damaged = async (
		name: string,
		directory: string,
		contents: string,
		keys: string,
	) => {
		writeFileSync(join(directory, "security.json"), contents);

		const damagedApp = await launchPane(context, { dataDir: directory });
		const atLaunch = await damagedApp.state();

		await damagedApp.send("render", {
			path: join(context.out, `security-damaged-${name}.png`),
		});

		await damagedApp.send("pinKeys", { keys });
		await Bun.sleep(1_500);
		const afterKeys = await damagedApp.state();

		const result = {
			lockedAtLaunch: atLaunch.security.locked,
			error: screen(atLaunch)?.error ?? null,
			lockedAfterKeys: afterKeys.security.locked,
			keptAside: existsSync(join(directory, "security.json.damaged")),
		};

		await damagedApp.browser.close();

		return result;
	};

	const withCopy = context.tempDir("pane-security-damaged-");
	await withPin(context, withCopy, "1234");

	const truncated = await damaged(
		"truncated",
		withCopy,
		readFileSync(join(withCopy, "security.json"), "utf8").slice(0, -12),
		"1234",
	);

	const garbage = await damaged(
		"garbage",
		context.tempDir("pane-security-damaged-"),
		'\u0000\u0000{"state":',
		"1234",
	);

	expect(
		before.profiles && before.profileData,
		`nothing to wipe: ${JSON.stringify(before)}`,
	);

	expect(!after.profileData, "the profile's engine data survived the wipe");

	// The wiped key is gone: the fresh Pane seals a key of its own, over a
	// new volume the wiped profile is not in.
	expect(
		before.volume &&
			!!before.key &&
			after.security?.state.key?.box !== before.key &&
			!after.profileInVolume,
		`the wiped key or volume survived: ${JSON.stringify({ before, after })}`,
	);

	expect(!after.migrationRecord, "a migration's record survived the wipe");

	expect(
		!after.staged && after.failures.length === 0,
		`the wipe left data: ${JSON.stringify({ staged: after.staged, failures: after.failures })}`,
	);

	expect(
		after.helpers.before > 0 && after.helpers.alive.length === 0,
		`the wiped instance's helpers outlived it: ${JSON.stringify(after.helpers)}`,
	);

	expect(
		quitWithCapsLock !== false,
		"⌘Q with Caps Lock did not quit at the PIN screen",
	);

	expect(
		truncated.lockedAtLaunch &&
			!truncated.lockedAfterKeys &&
			truncated.keptAside,
		`a damaged security.json with its key's copy: ${JSON.stringify(truncated)}`,
	);

	expect(
		garbage.lockedAtLaunch &&
			garbage.lockedAfterKeys &&
			!!garbage.error &&
			garbage.keptAside,
		`a damaged security.json with no copy: ${JSON.stringify(garbage)}`,
	);

	expect(
		!after.locked &&
			after.pinLength === null &&
			(after.security?.state.pin ?? null) === null,
		`the PIN survived: ${JSON.stringify(after)}`,
	);
};

export default security;
