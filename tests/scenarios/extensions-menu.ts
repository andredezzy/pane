import { join } from "node:path";
import {
	clickControl,
	isScreenLocked,
	mainWindowOf,
	windowState,
} from "../lib/cua";
import { type AppState, launchPane, type PaneApp } from "../lib/pane";
import { answerSheet, waitForSheet } from "../lib/surfaces";
import { expect, type Scenario } from "../run";

const DARK_READER = "eimadpbcbfnmbkopoojfekhnkhdbieeh";

// The toolbar's puzzle opens the extensions menu, as Aside's (Paper: 03.9
// Browser · Extensions): Manage all extensions opens the Extensions page,
// Visit Chrome Web Store opens it in a new tab, and each extension's row runs
// its action, its pin pins and unpins it with the menu still open, and its
// more menu manages or removes it. Esc and a click outside close the menu.
// On an unlocked screen each control is pressed with CUA; else the same
// controller calls run through the automation channel.
type Control = {
	// The automation channel's name for it, for a locked screen.
	name: string;
	title: string;
	id: string;
	extension?: string;
};

const puzzle: Control = {
	name: "puzzle",
	title: "Extensions",
	id: "pane.extensions",
};

const manage: Control = {
	name: "manage",
	title: "Manage all extensions",
	id: "pane.extensionsMenu.manage",
};

const webStore: Control = {
	name: "webStore",
	title: "Visit Chrome Web Store",
	id: "pane.extensionsMenu.webStore",
};

const row = (title: string): Control => ({
	name: "run",
	title,
	id: `pane.extensionsMenu.extension.${DARK_READER}`,
	extension: DARK_READER,
});

const pin = (pinned: boolean): Control => ({
	name: "pin",
	title: pinned ? "Unpin from toolbar" : "Pin to toolbar",
	id: `pane.extensionsMenu.pin.${DARK_READER}`,
	extension: DARK_READER,
});

// A row of the more menu.
const moreItem = (name: string, title: string, id: string): Control => ({
	name,
	title,
	id: `pane.extensionMoreMenu.${id}`,
	extension: DARK_READER,
});

const more: Control = {
	name: "more",
	title: "More",
	id: `pane.extensionsMenu.more.${DARK_READER}`,
	extension: DARK_READER,
};

async function press(
	app: PaneApp,
	locked: boolean,
	control: Control,
): Promise<void> {
	if (locked) {
		await app.send("extensionsMenu", {
			control: control.name,
			id: control.extension,
		});

		return;
	}

	const pid = app.browser.pid;

	// SwiftUI may redraw the control between reading it and pressing it (the
	// toolbar as the page settles): accessibility then calls it invalid
	// (-25202), and nothing was pressed, so it is read and pressed again.
	for (let attempt = 1; ; attempt++) {
		try {
			await clickControl(pid, await mainWindowOf(pid), control);

			return;
		} catch (error) {
			if (attempt === 3 || !String(error).includes("-25202")) {
				throw error;
			}

			await Bun.sleep(300);
		}
	}
}

async function openMenu(app: PaneApp, locked: boolean): Promise<AppState> {
	await press(app, locked, puzzle);

	return app.waitFor("the extensions menu", (state) => state.extensionsMenu);
}

// A row of the more menu, the menu reopened first if it closed: the window
// giving up key status (an extension's popup or tab taking it while the app
// is not frontmost) closes it, as leaving the window does.
async function pressInMoreMenu(
	app: PaneApp,
	locked: boolean,
	control: Control,
): Promise<void> {
	for (let attempt = 1; ; attempt++) {
		if ((await app.state()).extensionMoreMenu !== DARK_READER) {
			if (!(await app.state()).extensionsMenu) {
				await openMenu(app, locked);
			}

			await press(app, locked, more);

			await app.waitFor(
				"the more menu",
				(state) => state.extensionMoreMenu === DARK_READER,
			);
		}

		try {
			await press(app, locked, control);

			return;
		} catch (error) {
			const closed = (await app.state()).extensionMoreMenu !== DARK_READER;

			if (attempt === 3 || !closed) {
				throw error;
			}
		}
	}
}

const extensionsMenu: Scenario = async (context) => {
	const locked = await isScreenLocked();
	const app = await launchPane(context);
	const profile = await app.createProfile("Extensions");
	const url = context.fixtures.url("light.html?extensions-menu");
	const tab = await app.openTab(profile, url);
	await app.page(tab, url);

	const item = (state: AppState) =>
		(state.extensions[profile] ?? []).find((each) => each.id === DARK_READER);

	// None installed: the puzzle still shows, and the menu says so.
	await openMenu(app, locked);
	await Bun.sleep(300);
	await app.send("render", { path: join(context.out, "menu-empty.png") });

	// Esc closes it.
	await app.send("key", { key: "\u001b" });
	await app.waitFor("the menu closed by Esc", (state) => !state.extensionsMenu);

	// Visit Chrome Web Store: a new tab in the profile.
	await openMenu(app, locked);
	await press(app, locked, webStore);

	const visited = await app.waitFor(
		"the Web Store tab",
		(state) =>
			!state.extensionsMenu &&
			(state.profiles.find((each) => each.id === profile)?.tabs ?? []).some(
				(each) => each.url.startsWith("https://chromewebstore.google.com/"),
			),
	);

	// Manage all extensions: the Extensions page.
	await app.send("select", { tab });
	await openMenu(app, locked);
	await press(app, locked, manage);

	await app.waitFor(
		"the Extensions page",
		(state) => state.page === "extensions" && !state.extensionsMenu,
	);

	// Dark Reader, installed by its Web Store ID: pinned, as Chrome pins a new
	// extension.
	await app.send("installExtension", { id: DARK_READER, profile });

	// The install popup, under the puzzle (the extension has no button yet).
	const adding = await waitForSheet(app, "extensionInstall", 60_000);
	await Bun.sleep(300);
	await app.send("render", { path: join(context.out, "install-popup.png") });
	await answerSheet(app, adding.buttons?.[0] ?? "Add extension");

	const installed = await app.waitFor(
		"Dark Reader installed",
		(state) => item(state)?.enabled === true,
		120_000,
	);

	const name = item(installed)?.name ?? "Dark Reader";

	// Dark Reader opens its help page once installed, and the new tab takes
	// the window's key status a moment, which closes the menu, as leaving the
	// window does: the menu is used after it.
	await app.waitFor(
		"Dark Reader's help tab",
		(state) =>
			(state.profiles.find((each) => each.id === profile)?.tabs ?? []).some(
				(each) => each.url.startsWith("https://darkreader.org/"),
			),
		30_000,
	);

	await Bun.sleep(1_000);

	// Its pin unpins it and pins it again, the menu open throughout.
	await app.send("select", { tab });
	await openMenu(app, locked);
	await press(app, locked, pin(true));

	const unpinned = await app.waitFor(
		"unpinned from the menu",
		(state) => item(state)?.pinned === false && state.extensionsMenu,
	);

	await press(app, locked, pin(false));

	const pinned = await app.waitFor(
		"pinned again from the menu",
		(state) => item(state)?.pinned === true && state.extensionsMenu,
	);

	await Bun.sleep(300);
	await app.send("render", { path: join(context.out, "menu-light.png") });

	// As the screen shows it, over the page.
	if (locked) {
		context.defer("the menu as the screen shows it, over the page");
	} else {
		const pid = app.browser.pid;

		await windowState(
			pid,
			await mainWindowOf(pid),
			join(context.out, "menu-window.png"),
		);
	}

	// Its row runs its action: the popup, and the menu closes.
	await press(app, locked, row(name));

	const ran = await app.waitFor(
		"its popup",
		(state) =>
			state.extensionPopup?.extension === DARK_READER && !state.extensionsMenu,
		30_000,
	);

	// Its popover gone: the window it gives key back to, while the app is
	// not frontmost, resigns at once, and that closes the menu.
	await app.send("closeExtensionPopup");

	await app.waitFor(
		"the popup closed",
		(state) => state.extensionPopup === null,
	);

	await Bun.sleep(500);

	// More: Pane's own menu beside the row; Manage Extension opens the
	// Extensions page.
	await openMenu(app, locked);
	await press(app, locked, more);

	const moreMenu = await app.waitFor(
		"the more menu",
		(state) => state.extensionMoreMenu === DARK_READER,
	);

	await Bun.sleep(300);
	await app.send("render", { path: join(context.out, "more-menu.png") });

	await pressInMoreMenu(
		app,
		locked,
		moreItem("manageExtension", "Manage Extension", "manage"),
	);

	await app.waitFor(
		"the Extensions page again",
		(state) =>
			state.page === "extensions" &&
			!state.extensionsMenu &&
			state.extensionMoreMenu === null,
	);

	// The Dark theme, then a click outside closes the menu.
	await app.send("theme", { theme: "dark" });
	await app.send("select", { tab });
	const opened = await openMenu(app, locked);
	await Bun.sleep(300);
	await app.send("render", { path: join(context.out, "menu-dark.png") });

	// A click lands in the key window, as the user's would: the app comes
	// forward first (pressing with CUA does not make it key). While another
	// app keeps it, as when someone works on this Mac during the run, Esc
	// closes the menu instead, and the result says so.
	await app.send("activate");

	const key = await app
		.waitFor("the key window", (state) => state.window.key, 5_000)
		.then(
			() => true,
			() => false,
		);

	if (key) {
		const height = Number(
			opened.window.frame.match(/([\d.]+)\}\}$/)?.[1] ?? 800,
		);

		await app.send("clickWindow", { x: 600, y: height - 100 });
	} else {
		await app.send("key", { key: "\u001b" });
	}

	await app.waitFor(
		key ? "the menu closed by a click outside" : "the menu closed by Esc",
		(state) => !state.extensionsMenu,
	);

	// More › Remove from Pane asks first, in the popup under the puzzle.
	await openMenu(app, locked);
	await press(app, locked, more);

	await app.waitFor(
		"the more menu",
		(state) => state.extensionMoreMenu === DARK_READER,
	);

	await pressInMoreMenu(
		app,
		locked,
		moreItem("remove", "Remove from Pane", "remove"),
	);

	const removing = await waitForSheet(app, "extensionRemove");
	await Bun.sleep(300);
	await app.send("render", { path: join(context.out, "remove-popup.png") });

	expect(
		removing.title === `Remove \u201C${name}\u201D?` &&
			JSON.stringify(removing.buttons) === JSON.stringify(["Remove", "Cancel"]),
		`remove popup: ${JSON.stringify(removing)}`,
	);

	const removed = await answerSheet(app, removing.buttons?.[0] ?? "Remove");

	await app.waitFor(
		"Dark Reader removed",
		(state) => item(state) === undefined,
		30_000,
	);

	return {
		via: locked ? "automation" : "cua",
		webStoreTab: (
			visited.profiles.find((each) => each.id === profile)?.tabs ?? []
		).map((each) => each.url),
		installedPinned: item(installed)?.pinned,
		unpinned: item(unpinned)?.pinned,
		pinned: item(pinned)?.pinned,
		popup: ran.extensionPopup,
		moreMenu: moreMenu.extensionMoreMenu,
		install: adding,
		remove: { popup: removing, answer: removed },
		outsideClick: key
			? "closed the menu"
			: "not checked: another app kept the key window",
	};
};

export default extensionsMenu;
