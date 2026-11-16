import { join } from "node:path";
import { stats } from "../lib/benchmark";
import { canReadUI } from "../lib/computer-use";
import {
	type AppState,
	BUTTONS_TAKE_TAB,
	dataOpen,
	launchPane,
	type PaneApp,
	pressWindowKey,
	setPinBeforeLaunch,
} from "../lib/pane";
import { waitForExit } from "../lib/processes";
import { holdFile } from "../lib/tools";
import { accessibilityTree, describeElement } from "../lib/windows";
import { expect, type Scenario } from "../run";

// A PIN the disk didn't save says so at once (Paper 07.12, the user's call,
// 2026-11-16; ADR 0113): the PIN screen waits for security.json's write
// before it sinks, and where the file couldn't be written (another program
// holding it, a full disk; here Finder's Locked flag on the Mac, a handle
// with no delete sharing on Windows) it shows the flow's lines, with a
// button that keeps the seal the disk holds and Try again (07.11, 07.11.1).
// The seal in force stays the disk's until one lands, so the next launch and
// a lock open as the lines say. Before, the screen sank as if saved, the new
// PIN in force until Pane ended, and the next launch asked for the earlier
// one, or none, saying nothing. Each state is drawn in both themes, beside
// its board.

const CHANGE =
	"Pane couldn’t save the new PIN.\nYour earlier PIN still opens Pane.";

const REMOVE =
	"Pane couldn’t remove the PIN.\nPane still asks for it when it opens.";

const SETUP = "Pane couldn’t save the PIN.\nPane still opens without one.";

function screen(state: AppState) {
	return state.security.screen;
}

async function unlock(app: PaneApp, pin: string) {
	await app.send("pinKeys", { keys: pin });

	await app.waitFor(
		`unlocked with ${pin}`,
		(state) => !state.security.locked && dataOpen(state),
		60_000,
	);
}

// The screen says the disk didn't save the flow's seal: its lines, the two
// buttons, the pad taking no digit.
async function unsaved(app: PaneApp, mode: string, lines: string) {
	const state = await app.waitFor(
		`the ${mode} flow's lines that the disk didn't save it`,
		(state) => screen(state)?.mode === mode && screen(state)?.error === lines,
		30_000,
	);

	expect(
		screen(state)?.busy === true && !state.security.locked,
		`the screen after the refused write: ${JSON.stringify(screen(state))}`,
	);

	for (const button of ["pane.lockScreen.keep", "pane.lockScreen.tryAgain"]) {
		await app.middleOf(button);
	}

	return state;
}

// The screen gone, the seal in force the one with `pinLength` digits, or
// none.
async function settled(
	app: PaneApp,
	description: string,
	pinLength: number | null,
) {
	return app.waitFor(
		description,
		(state) =>
			!screen(state) &&
			!state.security.locked &&
			state.security.pinLength === pinLength,
		30_000,
	);
}

async function renderBoth(app: PaneApp, name: string, pointerAway = true) {
	// The pointer off every control, over the panel's empty right, so no key
	// is drawn lit by where the system's pointer happens to be.
	if (pointerAway) {
		await app.send("hoverWindow", { x: 1200, y: 100 });
	}

	for (const theme of ["light", "dark"] as const) {
		await app.send("theme", { theme });
		await app.render(`${name}-${theme}.png`, 800);
	}

	await app.send("theme", { theme: "system" });
}

// Milliseconds from sending `last` to the screen gone, read through the
// channel every 5 ms: the seal and its write together, or, for Try again, the
// write alone. A frame's own time is the Mac's signposts' (Save PIN,
// tests/rigs/views-lock.ts).
async function untilSunk(app: PaneApp, send: () => Promise<unknown>) {
	const started = performance.now();
	await send();

	for (;;) {
		if (!screen(await app.state())) {
			return performance.now() - started;
		}

		if (performance.now() - started > 30_000) {
			throw new Error("the PIN screen never sank");
		}

		await Bun.sleep(5);
	}
}

// Change PIN from `current` to `next`, all but the last digit typed.
async function changeUpToTheLastDigit(
	app: PaneApp,
	current: string,
	next: string,
) {
	await app.send("showPinScreen", { mode: "change" });
	await app.send("pinKeys", { keys: current });

	await app.waitFor(
		"Enter new PIN",
		(state) => screen(state)?.step === "enter",
		30_000,
	);

	await app.send("pinKeys", { keys: `${next}>${next.slice(0, -1)}` });

	await app.waitFor(
		"Confirm PIN, a digit short",
		(state) => screen(state)?.entered === next.length - 1,
		10_000,
	);
}

async function relaunch(
	context: Parameters<Scenario>[0],
	app: PaneApp,
	dataDir: string,
) {
	await app.quit();
	expect(await waitForExit(app.browser.pid, 30_000), "Pane didn't end at Quit");

	return launchPane(context, { dataDir });
}

const pinUnsaved: Scenario = async (context) => {
	const dataDir = context.tempDir("pane-pin-unsaved-");
	const security = join(dataDir, "security.json");
	await setPinBeforeLaunch(context, dataDir, "1234");
	let app = await launchPane(context, { dataDir });
	await app.send("setLockAfter", { minutes: null });
	await unlock(app, "1234");

	// Change PIN, security.json held: the screen says so at once, with Keep
	// earlier PIN and Try again; Try again while it's still held leaves the
	// lines, nothing shaking; Keep earlier PIN sinks the screen, the earlier
	// PIN in force.
	let release = await holdFile(security);

	try {
		await app.send("showPinScreen", { mode: "change" });
		await app.send("pinKeys", { keys: "1234" });

		await app.waitFor(
			"Enter new PIN",
			(state) => screen(state)?.step === "enter",
			30_000,
		);

		await app.send("pinKeys", { keys: "5678>5678" });
		const refused = await unsaved(app, "change", CHANGE);

		expect(
			refused.security.pinLength === 4,
			`the seal in force while the disk refuses the new one: ${refused.security.pinLength}`,
		);

		await renderBoth(app, "pin-unsaved-change");

		// What a screen reader reaches (VoiceOver's tree on the Mac, UI
		// Automation's on Windows): the lines, and each button by its title.
		if (await canReadUI()) {
			const tree = await accessibilityTree(app.browser.pid);
			const read = tree.map(describeElement);

			const said = (text: string) =>
				tree.some(
					(element) =>
						element.title.includes(text) ||
						element.value.includes(text) ||
						element.description.includes(text),
				);

			const button = (title: string) =>
				tree.some(
					(element) => element.title === title && /button/i.test(element.role),
				);

			expect(
				said("Pane couldn’t save the new PIN.") &&
					said("Your earlier PIN still opens Pane.") &&
					button("Keep earlier PIN") &&
					button("Try again"),
				`what a screen reader reaches of the refused PIN:\n${read.join("\n")}`,
			);
		} else {
			context.defer("what a screen reader reaches (the screen locked)");
		}

		await app.press("pane.lockScreen.tryAgain");
		await Bun.sleep(1_000);
		const again = await unsaved(app, "change", CHANGE);

		expect(
			screen(again)?.entered === 4,
			`the dots after Try again, still held: ${JSON.stringify(screen(again))}`,
		);

		// Where buttons take Tab (Windows), it gives the two the ring from the
		// PIN's field, round them in front, and Enter presses the one with it
		// (ADR 0126): here Keep earlier PIN. Elsewhere a press keeps it.
		if (BUTTONS_TAKE_TAB) {
			await pressWindowKey(context, app, "\t");

			await app.waitFor(
				"Keep earlier PIN given the keys by Tab",
				(state) => state.focus === "pane.lockScreen.keep",
			);

			if (context.foreground) {
				await pressWindowKey(context, app, "\t");

				await app.waitFor(
					"Try again given the keys by Tab",
					(state) => state.focus === "pane.lockScreen.tryAgain",
				);

				await renderBoth(app, "pin-unsaved-change-ring", false);
				await pressWindowKey(context, app, "\t");

				await app.waitFor(
					"Tab round the two, Keep earlier PIN again",
					(state) => state.focus === "pane.lockScreen.keep",
				);
			} else {
				context.defer("Tab round the two buttons (a real Tab, in front)");
			}

			await pressWindowKey(context, app, "\r");
		} else {
			await app.press("pane.lockScreen.keep");
		}

		await settled(app, "Keep earlier PIN: the screen gone, 1234 in force", 4);
	} finally {
		await release();
	}

	// The next launch opens with the earlier PIN, as the lines said.
	app = await relaunch(context, app, dataDir);
	await app.send("setLockAfter", { minutes: null });
	await unlock(app, "1234");

	// Change again, held, then let go: Return tries again, and the new PIN is
	// in force once security.json takes it.
	release = await holdFile(security);
	let released = false;

	try {
		await app.send("showPinScreen", { mode: "change" });
		await app.send("pinKeys", { keys: "1234" });

		await app.waitFor(
			"Enter new PIN",
			(state) => screen(state)?.step === "enter",
			30_000,
		);

		await app.send("pinKeys", { keys: "567890>567890" });
		await unsaved(app, "change", CHANGE);
		await release();
		released = true;
		await app.send("pinKeys", { keys: ">" });
		await settled(app, "Try again landed: 567890 in force", 6);
	} finally {
		if (!released) {
			await release();
		}
	}

	// Remove PIN, held: its own lines; Esc keeps the PIN.
	release = await holdFile(security);
	released = false;

	try {
		await app.send("showPinScreen", { mode: "remove" });
		await app.send("pinKeys", { keys: "567890" });
		await unsaved(app, "remove", REMOVE);
		await renderBoth(app, "pin-unsaved-remove");
		await app.send("pinKeys", { keys: "^" });
		await settled(app, "Keep PIN by Esc: 567890 still in force", 6);

		// Again, let go, and Try again: the PIN goes.
		await app.send("showPinScreen", { mode: "remove" });
		await app.send("pinKeys", { keys: "567890" });
		await unsaved(app, "remove", REMOVE);
		await release();
		released = true;
		await app.press("pane.lockScreen.tryAgain");
		await settled(app, "the PIN removed", null);
	} finally {
		if (!released) {
			await release();
		}
	}

	// Set up PIN, held: its own lines; Cancel leaves Pane without one.
	release = await holdFile(security);

	try {
		await app.send("showPinScreen", { mode: "setup" });
		await app.send("pinKeys", { keys: "2468>2468" });
		await unsaved(app, "setup", SETUP);
		await renderBoth(app, "pin-unsaved-setup");
		await app.press("pane.lockScreen.keep");
		await settled(app, "Cancel: no PIN", null);
	} finally {
		await release();
	}

	// The next launch opens with no PIN, as the disk holds none.
	app = await relaunch(context, app, dataDir);

	await app.waitFor(
		"opened with no PIN",
		(state) =>
			!state.security.locked &&
			dataOpen(state) &&
			state.security.pinLength === null,
		60_000,
	);

	// How long the screen now waits: Change's last digit to the screen gone,
	// the seal (on Windows the chip's new key) and its write; and Try again
	// to the screen gone, the write alone.
	await app.send("showPinScreen", { mode: "setup" });
	await app.send("pinKeys", { keys: "1234>1234" });
	await settled(app, "1234 set up", 4);
	const sealed: number[] = [];
	const written: number[] = [];
	let pin = "1234";

	for (let round = 0; round < 5; round++) {
		const next = pin === "1234" ? "5678" : "1234";
		await changeUpToTheLastDigit(app, pin, next);

		sealed.push(
			await untilSunk(app, () => app.send("pinKeys", { keys: next.slice(-1) })),
		);

		pin = next;

		const next2 = pin === "1234" ? "5678" : "1234";
		release = await holdFile(security);

		try {
			await changeUpToTheLastDigit(app, pin, next2);
			await app.send("pinKeys", { keys: next2.slice(-1) });
			await unsaved(app, "change", CHANGE);
		} finally {
			await release();
		}

		written.push(
			await untilSunk(app, () => app.press("pane.lockScreen.tryAgain")),
		);

		pin = next2;
	}

	return {
		lastDigitToSunkMs: stats(sealed),
		tryAgainToSunkMs: stats(written),
	};
};

export default pinUnsaved;
