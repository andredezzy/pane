import { launchPane, type PaneApp, pressWindowKey } from "../lib/pane";
import { KEYS } from "../lib/platform";
import { expect, type Scenario, type ScenarioContext } from "../run";

// The overlay on top takes a key first (ADR 0079): Quit Pane? sits over the
// extensions menu and over Settings' App icon picker, so Esc closes Quit
// Pane? first and the one under it with the next. Either can only open
// first: Quit Pane?'s scrim takes a press, as its card every key, as the
// Swift's modal did, so the press on the puzzle closes Quit Pane? and opens
// nothing under it.
// Four monitors used to decide in AppKit's order, which is not the drawing
// order: one Esc closed the menu hidden under the panel. In Views the
// overlay on top holds the keys. ⌘Q is posted through AppKit's queue, which
// the menu bar takes without the front; Esc is the window's key: posted with
// the front, through Views' own dispatch without it. And Lock Pane's key
// goes ahead of the page (ADR 0019, the user's call of 2026-11-17): a page
// that keeps ⌃⌘L, Ctrl+Shift+L on Windows, as Docs' and Sheets' align left
// keeps it there, locks Pane all the same, by a real key in front.

type Showing = { quit: boolean; menu: boolean };

async function pressEscape(
	context: ScenarioContext,
	app: PaneApp,
): Promise<Showing> {
	const before = await app.state();

	await pressWindowKey(context, app, "\u001b");

	const after = await app.waitFor(
		"Esc taken",
		(state) =>
			state.quitConfirmation !== before.quitConfirmation ||
			state.extensionsMenu !== before.extensionsMenu,
		5_000,
	);

	return { quit: after.quitConfirmation, menu: after.extensionsMenu };
}

const openMenu = (app: PaneApp) =>
	app.send("extensionsMenu", { control: "puzzle" });

const openQuit = (app: PaneApp) => app.send("key", KEYS.quit);

// The menu, then ⌘Q: Quit Pane? over it.
async function openBoth(app: PaneApp): Promise<void> {
	await app.send("activate");
	await app.waitFor("the key window", (state) => state.window.key, 10_000);
	await openMenu(app);
	await app.waitFor("the menu", (state) => state.extensionsMenu);
	await openQuit(app);

	await app.waitFor(
		"Quit Pane? over the menu",
		(state) => state.quitConfirmation && state.extensionsMenu,
	);
}

const keyPrecedence: Scenario = async (context) => {
	const app = await launchPane(context);
	const profile = await app.createProfile("Keys");
	const url = context.fixtures.url("smoke.html?keys");
	const { tab, page } = await app.openPage(profile, url);

	// Quit Pane? over the App icon picker: ⌘Q can only come second, as the
	// picker's scrim takes a click and Quit Pane? every key.
	await app.send("showPage", { page: "settings" });
	await app.send("appIconPicker", { open: true });
	await app.waitFor("the picker", (state) => state.appIconPicker);
	await app.send("key", KEYS.quit);

	await app.waitFor(
		"Quit Pane? over the picker",
		(state) => state.quitConfirmation && state.appIconPicker,
	);

	const picker: { quit: boolean; picker: boolean }[] = [];

	for (let step = 0; step < 2; step++) {
		const before = await app.state();
		await pressWindowKey(context, app, "\u001b");

		const after = await app.waitFor(
			"Esc taken",
			(state) =>
				state.quitConfirmation !== before.quitConfirmation ||
				state.appIconPicker !== before.appIconPicker,
			5_000,
		);

		picker.push({ quit: after.quitConfirmation, picker: after.appIconPicker });
	}

	expect(
		JSON.stringify(picker) ===
			JSON.stringify([
				{ quit: false, picker: true },
				{ quit: false, picker: false },
			]),
		`Quit Pane? over the picker: Esc, then Esc, left ${JSON.stringify(picker)}`,
	);

	await app.send("showPage", { page: "browser" });

	// Quit Pane? over the extensions menu, in both orders, which needs Pane
	// in front, and the menu, which the Views line draws since commit 6bf3b75.
	if (!("extensionsMenu" in (await app.state()))) {
		context.defer(
			"Quit Pane? over the extensions menu: no extensions menu is drawn",
		);

		return;
	}

	if (!context.foreground) {
		context.defer(
			"Quit Pane? over the extensions menu, and Lock Pane's key past a page that keeps it: they need Pane in front (run.ts --foreground)",
		);

		return;
	}

	await openBoth(app);

	const steps = [
		await pressEscape(context, app),
		await pressEscape(context, app),
	];

	expect(
		JSON.stringify(steps) ===
			JSON.stringify([
				{ quit: false, menu: true },
				{ quit: false, menu: false },
			]),
		`Quit Pane? over the menu: Esc, then Esc, left ${JSON.stringify(steps)}`,
	);

	// Quit Pane? first: the press on the puzzle lands on its scrim.
	await openQuit(app);
	await app.waitFor("Quit Pane?", (state) => state.quitConfirmation);
	await openMenu(app);

	const pressed = await app.waitFor(
		"the press on Quit Pane?'s scrim",
		(state) => !state.quitConfirmation,
		5_000,
	);

	expect(
		!pressed.extensionsMenu,
		"the press under Quit Pane? opened the extensions menu",
	);

	// Lock Pane's key past a page that keeps it, once a PIN lets Pane lock.
	await app.send("showPinScreen", { mode: "setup" });

	await app.waitFor(
		"Set up PIN",
		(state) => state.security.screen?.mode === "setup",
	);

	await app.send("pinKeys", { keys: "1234>" });

	await app.waitFor(
		"the PIN asked again",
		(state) => state.security.screen?.step === "confirm",
	);

	await app.send("pinKeys", { keys: "1234" });

	await app.waitFor(
		"the PIN set up",
		(state) => state.security.screen === null && state.security.pinLength === 4,
	);

	await app.send("select", { tab });

	await app.waitFor(
		"the page with the keys",
		(state) => state.focus === "pane.content",
		10_000,
	);

	// The page keeps the key, as an editor that takes it does.
	await page.evaluate(`addEventListener("keydown", (event) => {
		if (event.code === "KeyL" && event.ctrlKey && (event.metaKey || event.shiftKey)) {
			event.preventDefault();
		}
	}, true)`);

	await app.send("key", KEYS.lock);
	await app.send("releaseModifiers");

	const locked = await app
		.waitFor(
			"Pane locked past the page",
			(state) => state.security.locked,
			5_000,
		)
		.then(
			() => true,
			() => false,
		);

	expect(locked, "a page that keeps Lock Pane's key kept Pane open");
	await app.send("pinKeys", { keys: "1234" });
	await app.waitFor("unlocked", (state) => !state.security.locked, 60_000);
};

export default keyPrecedence;
