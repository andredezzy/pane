import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { stats } from "../lib/benchmark";
import { isScreenLocked } from "../lib/cua";
import {
	type AppState,
	launchPane,
	type PaneApp,
	pressWindowKey,
	setUpPin,
} from "../lib/pane";
import { PRIMARY } from "../lib/platform";
import { signpostIntervals } from "../lib/signposts";
import { accessibilityTree } from "../lib/windows";
import { expect, type Scenario } from "../run";

// L1, the PIN screen (docs/plans/views-migration.md, Phase 3), on the Views
// build, in the background. Its renders in every state Paper 07.1 to 07.8
// draws, named as the Swift's surfaces named theirs where it rendered them
// (surfaces-pin-setup-empty … surfaces-lock-update-available), the rest by
// Paper's boards: a digit's key under the pointer and pressed (03.6), the
// data's move after the PIN (07.5), a key that won't read (07.6), and a
// Keychain that won't give it (07.7); 07.8 is data-volume's. The numpad's
// keys, Delete, Continue, and the back arrow by real presses, each read back
// from what the PIN screen holds. Then how long a digit takes to its dot, a
// wrong PIN to its shake, the right one to the Lock sinking, and Lock Pane
// to the screen's first frame, from the app's signposts.
//
//   bun tests/run.ts --binary ~/chromium/src/out/Release/Pane.app/Contents/MacOS/Pane \
//     --out <dir> [--appearance light|dark] tests/rigs/views-lock.ts

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

function screen(state: AppState) {
	return state.security.screen;
}

// What VoiceOver reaches of the window at the Lock (ADR 0071): the PIN
// screen alone, its dots and what it says read, and nothing under it, which
// it reached until the Views build left the window's other layers out
// (UpdateAccessibleLayers) and exposed the screen's text, which a view that
// holds the keys hid (ExposeToScreenReaders). Undefined on a locked screen,
// which hides every window from accessibility.
async function readAtTheLock(
	context: Parameters<Scenario>[0],
	app: PaneApp,
	said: string[],
): Promise<string[] | undefined> {
	if (await isScreenLocked()) {
		context.defer(
			"what a screen reader reaches at the Lock (the screen locked)",
		);

		return undefined;
	}

	const tree = await accessibilityTree(app.browser.pid);

	const read = tree
		.map((element) =>
			[element.title, element.value].filter(Boolean).join(" = "),
		)
		.filter(Boolean);

	const under = ["New", "Settings", "Sidebar width", "Back", "Address"].filter(
		(name) => tree.some((element) => element.title === name),
	);

	expect(
		under.length === 0,
		`a screen reader reaches under the Lock: ${under.join(", ")}`,
	);

	for (const text of said) {
		expect(
			tree.some((element) => element.title === text || element.value === text),
			`a screen reader doesn't read "${text}" at the Lock:\n${read.join("\n")}`,
		);
	}

	return read;
}

// A frame's middle, from the window's top left.
async function middleOf(app: PaneApp, name: string) {
	const [x = 0, y = 0, width = 0, height = 0] =
		(await app.frames(name))[name] ?? [];

	expect(width > 0 && height > 0, `${name} does not show`);

	return { x: x + width / 2, y: y + height / 2 };
}

// A press and its release at a control's middle, through Views' own
// dispatch, which reaches the window in the background.
async function press(app: PaneApp, name: string) {
	const at = await middleOf(app, name);
	await app.send("pointerWindow", { phase: "down", ...at });
	await app.send("pointerWindow", { phase: "up", ...at });
}

// The pointer off every control, over the panel's empty right.
const away = (app: PaneApp) => app.send("hoverWindow", { x: 1200, y: 100 });

async function keys(
	app: PaneApp,
	typed: string,
	description: string,
	until: (state: AppState) => boolean,
) {
	await app.send("pinKeys", { keys: typed });

	return app.waitFor(description, until, 10_000);
}

// Settings' PIN screen, as surfaces' pinScreens: setting one up (empty, two
// digits, long enough, confirming, two that differ), changing it, removing
// it; a digit's key under the pointer and pressed; and each control by a
// real press.
async function pinScreens(context: Parameters<Scenario>[0], log: Log) {
	const app = await launchPane(context);

	try {
		const work = await app.createProfile("Work", "BLUE");

		const { tab } = await app.openPage(
			work,
			context.fixtures.url("nav-a.html?lock"),
		);

		expect(!!tab, "no tab");

		await app.send("showPinScreen", { mode: "setup" });
		await app.waitFor("Set up", (state) => screen(state)?.mode === "setup");
		log.focusAtSetup = (await app.state()).focus;
		await app.render("surfaces-pin-setup-empty.png", 800);

		await keys(app, "12", "two digits", (s) => screen(s)?.entered === 2);
		await app.render("surfaces-pin-setup-typing.png");
		await keys(app, "34", "four", (s) => screen(s)?.entered === 4);
		await app.render("surfaces-pin-setup-continue.png");

		// A fifth and a sixth: the dots grow, and Delete takes one back, by
		// presses.
		await press(app, "pane.lockScreen.digit.5");
		await press(app, "pane.lockScreen.digit.6");
		const six = await app.waitFor("six", (s) => screen(s)?.entered === 6);
		await press(app, "pane.lockScreen.backspace");
		await press(app, "pane.lockScreen.backspace");
		const back = await app.waitFor("four", (s) => screen(s)?.entered === 4);

		log.pressed = {
			six: screen(six)?.dots,
			afterDelete: screen(back)?.dots,
		};

		expect(
			screen(six)?.dots === 6 && screen(back)?.dots === 4,
			`the dots by presses: ${JSON.stringify(log.pressed)}`,
		);

		// Continue, by a press.
		await press(app, "pane.lockScreen.continue");
		await app.waitFor("confirming", (s) => screen(s)?.step === "confirm");
		await app.render("surfaces-pin-setup-confirm.png", 400);

		await keys(
			app,
			"1235",
			"the mismatch, shaken",
			(s) => screen(s)?.error === "PINs don't match" && !screen(s)?.busy,
		);

		await app.render("surfaces-pin-setup-mismatch.png", 500);

		// A digit's key under the pointer, then held down (Paper 03.6).
		const five = await middleOf(app, "pane.lockScreen.digit.5");
		await app.send("hoverWindow", five);
		await app.render("pin-key-hover.png", 400);
		await app.send("pointerWindow", { phase: "down", ...five });
		await app.render("pin-key-pressed.png", 400);
		await app.send("pointerWindow", { phase: "up", ...five });
		await keys(app, "<", "the digit taken back", (s) => !screen(s)?.entered);
		await away(app);

		await keys(
			app,
			"1234",
			"the PIN set up",
			(s) => screen(s) === null && s.security.pinLength === 4,
		);

		log.pageAfterSetup = (await app.state()).page;

		await app.send("showPinScreen", { mode: "change" });
		await app.waitFor("Change", (s) => screen(s)?.mode === "change");
		await app.render("surfaces-pin-change-current.png", 800);

		await keys(
			app,
			"1234",
			"the new PIN asked",
			(s) => screen(s)?.step === "enter",
		);

		await app.render("surfaces-pin-change-new.png");

		await keys(
			app,
			"5678>",
			"confirming",
			(s) => screen(s)?.step === "confirm",
		);

		await app.render("surfaces-pin-change-confirm.png");

		await keys(
			app,
			"5678",
			"the PIN changed",
			(s) => screen(s) === null && s.security.pinLength === 4,
		);

		// The back arrow leaves Remove, by a press; Esc, as typed, too.
		await app.send("showPinScreen", { mode: "remove" });
		await app.waitFor("Remove", (s) => screen(s)?.mode === "remove");
		await app.render("surfaces-pin-remove-current.png", 800);
		await press(app, "pane.lockScreen.back");
		await app.waitFor("Back", (s) => screen(s) === null);
		await app.send("showPinScreen", { mode: "remove" });
		await app.waitFor("Remove", (s) => screen(s)?.mode === "remove");
		await keys(app, "^", "Esc", (s) => screen(s) === null);

		await app.send("showPinScreen", { mode: "remove" });
		await app.waitFor("Remove", (s) => screen(s)?.mode === "remove");

		await keys(
			app,
			"5678",
			"the PIN removed",
			(s) => screen(s) === null && s.security.pinLength === null,
		);

		log.focusAfterRemove = (await app.state()).focus;
	} finally {
		await app.browser.close();
	}
}

// The Lock (Lock Pane, Paper 07.9 and 07.1): at rest, two digits in, a wrong
// PIN, and four (the last attempt); the right PIN opens it.
async function lockScreens(context: Parameters<Scenario>[0], log: Log) {
	const app = await launchPane(context);

	try {
		const work = await app.createProfile("Work", "BLUE");

		const { tab } = await app.openPage(
			work,
			context.fixtures.url("nav-a.html?lock"),
		);

		await setUpPin(app, "1234");
		await app.send("showPage", { page: "browser" });
		await app.send("select", { tab });

		const reply = await app.send("lockNow");
		log.lockReply = reply.state.security.screen?.mode ?? null;

		await app.waitFor(
			"the Lock",
			(s) => s.security.locked && screen(s)?.mode === "unlock",
		);

		log.focusAtLock = (await app.state()).focus;
		await app.render("surfaces-lock-unlock.png", 800);
		log.readAtTheLock = await readAtTheLock(context, app, ["0 of 4 digits"]);

		await keys(app, "12", "two digits", (s) => screen(s)?.entered === 2);
		await app.render("surfaces-lock-typing.png");
		await keys(app, "<<", "none", (s) => screen(s)?.entered === 0);

		// The window resigns key, as when the app in use takes the front back
		// (a launch in the background hands it back at once, and a user typing
		// elsewhere keeps it): Views leaves the focus on no view, keeping the
		// PIN screen for when the window is key again. The PIN still reaches
		// it, as a user's keys do once they are in Pane.
		// In front, the key window ignores it (Views' delegate), so this needs
		// the background.
		if ((await app.state()).window.key) {
			context.defer("the PIN after the window resigned key (in front)");
		} else {
			await app.send("resignKey");
			log.focusAfterResigningKey = (await app.state()).focus;

			expect(
				log.focusAfterResigningKey === "none",
				`the focus once the window resigned key: ${log.focusAfterResigningKey}`,
			);

			await keys(
				app,
				"12",
				"two digits, the window having resigned key",
				(s) => screen(s)?.entered === 2,
			);

			log.focusAfterTheKeys = (await app.state()).focus;
			await keys(app, "<<", "none again", (s) => screen(s)?.entered === 0);
		}

		for (let attempt = 1; attempt <= 4; attempt++) {
			// The numpad's keys, by presses, the first time.
			if (attempt === 1) {
				for (const digit of "0000") {
					await press(app, `pane.lockScreen.digit.${digit}`);
				}
			} else {
				await app.send("pinKeys", { keys: "0000" });
			}

			await app.waitFor(
				`wrong PIN ${attempt}`,
				(s) =>
					s.security.failedAttempts === attempt &&
					screen(s)?.entered === 0 &&
					!screen(s)?.busy,
				10_000,
			);

			if (attempt === 1) {
				await app.render("surfaces-lock-wrong-pin.png", 500);

				log.readAfterAWrongPin = await readAtTheLock(context, app, [
					"4 attempts remaining",
				]);
			}
		}

		await app.render("surfaces-lock-last-attempt.png", 500);
		await keys(app, "1234", "unlocked", (s) => !s.security.locked);

		await app.waitFor(
			"the tab back",
			(s) => s.activeTabID === tab && s.security.data.attached,
			60_000,
		);

		log.focusAfterUnlock = (await app.state()).focus;
	} finally {
		await app.browser.close();
	}
}

// The update on offer beside the Lock's status (Paper 07.1).
async function updateAtTheLock(context: Parameters<Scenario>[0]) {
	const app = await launchPane(context, {
		env: {
			PANE_UPDATE_URL: context.fixtures.url("releases/latest?tag=v99.0.0"),
			PANE_UPDATE_DOWNLOADS: context.tempDir("pane-lock-updates-"),
			PANE_UPDATE_OPEN: "0",
		},
	});

	try {
		await app.waitFor(
			"the update offered",
			(s) => s.update.status === "available",
			15_000,
		);

		await setUpPin(app, "1234");
		await app.send("lockNow");
		await app.waitFor("the Lock", (s) => s.security.locked);
		await app.render("surfaces-lock-update-available.png", 800);

		// Its action under the pointer.
		await app.send(
			"hoverWindow",
			await middleOf(app, "pane.lockScreen.update"),
		);

		await app.render("lock-update-hover.png", 400);
		await away(app);
		await keys(app, "1234", "unlocked", (s) => !s.security.locked);
	} finally {
		await app.browser.close();
	}
}

// The data's one-time move into its volume after the PIN (Paper 07.5): an
// earlier Pane's plain profiles.json and its profile's plain folder, 600 MB
// in 300 files, which the PIN's opening moves in behind the panel.
async function dataMove(context: Parameters<Scenario>[0], log: Log) {
	const dataDir = context.tempDir("pane-lock-move-");
	const id = "0e3f5d7a-1b2c-4d5e-8f90-a1b2c3d4e5f6";
	writeFileSync(join(dataDir, "security.json"), JSON.stringify(PIN_1234));

	writeFileSync(
		join(dataDir, "profiles.json"),
		JSON.stringify({
			state: {
				profiles: [
					{
						id,
						name: "Moved",
						color: "BLUE",
						tabs: [],
						createdAt: "2026-10-30T00:00:00.000Z",
						updatedAt: "2026-10-30T00:00:00.000Z",
					},
				],
			},
			version: 0,
		}),
	);

	const plain = join(dataDir, "Engine", id, "Default");
	mkdirSync(plain, { recursive: true });
	const block = new Uint8Array(2 * 1024 * 1024);

	for (let index = 0; index < 300; index++) {
		crypto.getRandomValues(block.subarray(0, 65_536));
		writeFileSync(join(plain, `file-${index}`), block);
	}

	const app = await launchPane(context, { dataDir });

	try {
		await app.send("pinKeys", { keys: "1234" });
		let rendered = false;

		for (const until = Date.now() + 60_000; Date.now() < until; ) {
			const state = await app.state();
			const moving = /^moving (.+)/.exec(state.security.data.phase);

			if (moving && Number(moving[1]) > 0.15 && !rendered) {
				await app.render("lock-data-move.png", 0);
				log.moveRendered = Number(moving[1]);
				rendered = true;
			}

			if (state.security.data.phase === "open") {
				break;
			}

			await Bun.sleep(20);
		}

		log.moveSeen = rendered;
	} finally {
		await app.browser.close();
	}
}

// A key that won't read (07.6) and a Keychain that won't give it (07.7).
async function unreadable(context: Parameters<Scenario>[0], log: Log) {
	const garbageDir = context.tempDir("pane-lock-garbage-");
	writeFileSync(join(garbageDir, "security.json"), '\u0000\u0000{"state":');
	const garbage = await launchPane(context, { dataDir: garbageDir });

	try {
		log.unreadable = screen(await garbage.state())?.error ?? null;
		await garbage.render("lock-unreadable-key.png", 800);
	} finally {
		await garbage.browser.close();
	}

	const denied = await launchPane(context, {
		env: { PANE_MOCK_KEYCHAIN_DENIES: "1" },
	});

	try {
		const state = await denied.waitFor(
			"07.7",
			(s) => s.security.locked && !!screen(s)?.error,
		);

		log.keychainDenied = screen(state)?.error ?? null;
		await denied.render("lock-keychain-denied.png", 800);
	} finally {
		await denied.browser.close();
	}
}

// From each input to the frame on screen: a digit to its dot, the digit that
// completes a wrong PIN to its shake, the right one to the Lock sinking, and
// Lock Pane to the screen's first frame rising.
async function timing(context: Parameters<Scenario>[0], log: Log) {
	const rounds = 8;

	const intervals = await signpostIntervals(async () => {
		const app = await launchPane(context);

		try {
			const work = await app.createProfile("Timing", "BLUE");
			await app.openPage(work, context.fixtures.url("nav-a.html?timing"));
			await setUpPin(app, "1234");
			await app.send("showPage", { page: "browser" });
			await Bun.sleep(1_000);

			for (let round = 0; round < rounds; round++) {
				await app.send("lockNow");

				await app.waitFor(
					"the Lock",
					(s) => s.security.locked && !s.security.data.attached,
					30_000,
				);

				await Bun.sleep(600);

				// One digit at a time, as fingers type.
				for (const digit of "9999") {
					await app.send("pinKeys", { keys: digit });
					await Bun.sleep(120);
				}

				await app.waitFor(
					"the shake over",
					(s) => s.security.failedAttempts === 1 && !screen(s)?.busy,
					10_000,
				);

				for (const digit of "1234") {
					await app.send("pinKeys", { keys: digit });
					await Bun.sleep(120);
				}

				await app.waitFor(
					"open",
					(s) => !s.security.locked && s.security.data.attached,
					60_000,
				);

				await Bun.sleep(800);
			}
		} finally {
			await app.browser.close();
		}

		return undefined;
	});

	log.timing = Object.fromEntries(
		Object.entries(intervals)
			.filter(([name]) =>
				["PIN digit", "Wrong PIN", "Unlock", "Lock"].includes(name),
			)
			.map(([name, values]) => [name, stats(values)]),
	);
}

// A Lock takes Quit Pane?'s place, as it takes the switcher's
// (UiApp::LockNow): nothing of the window waits under the PIN screen, which
// alone takes the keys. Esc at it leaves it up, and the PIN brings no Quit
// Pane? back. The Swift's drew the PIN screen over Quit Pane?, which waited
// under it (KeyRoutingTests.swift's pinOverQuit). Through Views' own dispatch
// in the background, posted as real keys with --foreground.
async function lockOverQuit(context: Parameters<Scenario>[0], log: Log) {
	const app = await launchPane(context);

	try {
		await setUpPin(app, "1234");
		await app.send("key", { key: "q", modifiers: [PRIMARY] });
		await app.waitFor("Quit Pane?", (state) => state.quitConfirmation);
		await app.send("lockNow");

		await app.waitFor(
			"the Lock in Quit Pane?'s place",
			(state) => state.security.locked && !state.quitConfirmation,
		);

		await pressWindowKey(context, app, "\u001b");
		await Bun.sleep(500);
		const held = await app.state();

		expect(
			held.security.locked &&
				screen(held)?.mode === "unlock" &&
				!held.quitConfirmation,
			`Esc at the Lock: ${JSON.stringify({
				locked: held.security.locked,
				screen: screen(held),
				quit: held.quitConfirmation,
			})}`,
		);

		await keys(app, "1234", "unlocked", (state) => !state.security.locked);
		await Bun.sleep(500);
		const after = await app.state();
		expect(!after.quitConfirmation, "Quit Pane? came back after the PIN");

		log.lockOverQuit = {
			quitAtTheLock: "dismissed",
			escAtTheLock: "held",
			afterThePin: after.focus,
		};
	} finally {
		await app.browser.close();
	}
}

type Log = Record<string, unknown>;

const viewsLock: Scenario = async (context) => {
	const log: Log = {};

	await pinScreens(context, log);
	await lockScreens(context, log);
	await updateAtTheLock(context);
	await lockOverQuit(context, log);
	await unreadable(context, log);
	await dataMove(context, log);

	if (!process.env.PANE_LOCK_NO_TIMING) {
		await timing(context, log);
	}

	return log;
};

export default viewsLock;
