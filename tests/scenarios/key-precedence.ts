import { launchPane, type PaneApp } from "../lib/pane";
import { expect, type Scenario, type ScenarioContext } from "../run";

// The overlay on top takes a key first (ADR 0079): Quit Pane? sits over the
// extensions menu and over Settings' App icon picker, whichever opened
// first, so Esc closes Quit Pane? first and the one under it with the next.
// Four monitors used to decide in AppKit's order, which is not the drawing
// order: one Esc closed the menu hidden under the panel. In Views the
// overlay on top holds the keys. ⌘Q is posted through AppKit's queue, which
// the menu bar takes without the front; Esc is the window's key: posted with
// the front, through Views' own dispatch without it.

type Showing = { quit: boolean; menu: boolean };

async function press(
	context: ScenarioContext,
	app: PaneApp,
	key: string,
): Promise<void> {
	if (!context.foreground) {
		await app.send("dispatchKey", { key });

		return;
	}

	await app.send("activate");
	await app.waitFor("the key window", (state) => state.window.key, 10_000);
	await app.send("key", { key });
}

async function pressEscape(
	context: ScenarioContext,
	app: PaneApp,
): Promise<Showing> {
	const before = await app.state();

	await press(context, app, "\u001b");

	const after = await app.waitFor(
		"Esc taken",
		(state) =>
			state.quitConfirmation !== before.quitConfirmation ||
			state.extensionsMenu !== before.extensionsMenu,
		5_000,
	);

	return { quit: after.quitConfirmation, menu: after.extensionsMenu };
}

async function openBoth(app: PaneApp, first: "menu" | "quit"): Promise<void> {
	const openMenu = () => app.send("extensionsMenu", { control: "puzzle" });
	const openQuit = () => app.send("key", { key: "q", modifiers: ["command"] });

	await app.send("activate");
	await app.waitFor("the key window", (state) => state.window.key, 10_000);

	if (first === "menu") {
		await openMenu();
		await app.waitFor("the menu", (state) => state.extensionsMenu);
		await openQuit();
	} else {
		await openQuit();
		await app.waitFor("Quit Pane?", (state) => state.quitConfirmation);
		await openMenu();
	}

	await app.waitFor(
		"Quit Pane? over the menu",
		(state) => state.quitConfirmation && state.extensionsMenu,
	);
}

const keyPrecedence: Scenario = async (context) => {
	const app = await launchPane(context);
	const profile = await app.createProfile("Keys");
	const url = context.fixtures.url("smoke.html?keys");
	await app.openPage(profile, url);

	// Quit Pane? over the App icon picker: ⌘Q can only come second, as the
	// picker's scrim takes a click and Quit Pane? every key.
	await app.send("showPage", { page: "settings" });
	await app.send("appIconPicker", { open: true });
	await app.waitFor("the picker", (state) => state.appIconPicker);
	await app.send("key", { key: "q", modifiers: ["command"] });

	await app.waitFor(
		"Quit Pane? over the picker",
		(state) => state.quitConfirmation && state.appIconPicker,
	);

	const picker: { quit: boolean; picker: boolean }[] = [];

	for (let step = 0; step < 2; step++) {
		const before = await app.state();
		await press(context, app, "\u001b");

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
	// in front, and the menu, which X1 draws on the Views line.
	if (!("extensionsMenu" in (await app.state()))) {
		context.defer(
			"Quit Pane? over the extensions menu: no extensions menu is drawn (X1)",
		);

		return;
	}

	if (!context.foreground) {
		context.defer(
			"Quit Pane? over the extensions menu: it needs Pane in front (run.ts --foreground)",
		);

		return;
	}

	const orders: Record<string, Showing[]> = {};

	for (const first of ["menu", "quit"] as const) {
		await openBoth(app, first);

		orders[first] = [
			await pressEscape(context, app),
			await pressEscape(context, app),
		];
	}

	for (const [first, steps] of Object.entries(orders)) {
		expect(
			JSON.stringify(steps) ===
				JSON.stringify([
					{ quit: false, menu: true },
					{ quit: false, menu: false },
				]),
			`the ${first} first: Esc, then Esc, left ${JSON.stringify(steps)}`,
		);
	}
};

export default keyPrecedence;
