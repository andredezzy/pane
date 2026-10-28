import { launchPane, type PaneApp } from "../lib/pane";
import { expect, type Scenario } from "../run";

// One key monitor asks the overlays that show, topmost first (PaneKit
// KeyRouting). Quit Pane? sits over the extensions menu, whichever opened
// first, so Esc closes Quit Pane? first and the menu with the next one. Four
// monitors used to decide in AppKit's order, which is not the drawing order:
// one Esc closed the menu hidden under the panel.

type Showing = { quit: boolean; menu: boolean };

async function press(app: PaneApp, key: string): Promise<void> {
	await app.send("activate");
	await app.waitFor("the key window", (state) => state.window.key, 10_000);
	await app.send("key", { key });
}

async function pressEscape(app: PaneApp): Promise<Showing> {
	const before = await app.state();

	await press(app, "\u001b");

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
	await app.page(await app.openTab(profile, url), url);

	const orders: Record<string, Showing[]> = {};

	for (const first of ["menu", "quit"] as const) {
		await openBoth(app, first);
		orders[first] = [await pressEscape(app), await pressEscape(app)];
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

	return { orders };
};

export default keyPrecedence;
