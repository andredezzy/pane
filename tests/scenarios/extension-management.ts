import { existsSync } from "node:fs";
import { join } from "node:path";
import {
	DETAILS,
	detailsShown,
	EXTENSIONS_PAGE,
	extensionsPage,
	ITEMS,
	listed,
	press,
} from "../lib/chrome-extensions";
import { DARK_READER, installById } from "../lib/extensions";
import type { AppState, ExtensionState } from "../lib/pane";
import { launchPane } from "../lib/pane";
import { ENGINE_FOLDER } from "../lib/platform";
import { poll } from "../lib/poll";
import { answerSheet, WindowWatcher, waitForSheet } from "../lib/surfaces";
import { expect, type Scenario } from "../run";

// A profile's extensions are managed on Chrome's own page in its tab (D3,
// ADR 0062): the extensions menu's Manage all extensions opens
// chrome://extensions there, and opens it again in the same tab. Adding
// asks in Pane's dialog (ADR 0056): declined, nothing is installed, on the
// page or on disk; accepted, the extension lists on the page, pinned to the
// toolbar. The card's switch turns it off and on, Details' Pin to toolbar
// unpins and pins it, and Remove from Pane, in the extensions menu's more
// menu, asks in Pane's popup and takes it off the page. Updates need no
// control: extension-auto-update checks them. Chrome's page is pressed with
// real clicks on it (CDP's Input), Pane's views with real clicks through
// the channel, in the background too.
const extensionManagement: Scenario = async (context) => {
	const app = await launchPane(context);
	const watcher = new WindowWatcher(app).start();
	const profile = await app.createProfile("Manage");
	const url = context.fixtures.url("light.html?manage");
	const { tab: fixture } = await app.openPage(profile, url);

	const item = (state: AppState): ExtensionState | undefined =>
		(state.extensions[profile] ?? []).find((each) => each.id === DARK_READER);

	// Chrome's page, as the extensions menu's Manage all extensions opens
	// it, from the profile's page.
	const manage = async () => {
		await app.send("select", { tab: fixture });
		await app.send("extensionsMenu", { control: "puzzle" });
		await app.waitFor("the extensions menu", (state) => state.extensionsMenu);
		await app.send("extensionsMenu", { control: "manage" });

		return extensionsPage(app, profile, EXTENSIONS_PAGE);
	};

	const opened = await manage();

	expect(
		(await opened.page.evaluate<string>("document.title")) === "Extensions",
		"Chrome's page is not titled Extensions",
	);

	// Declined: Cancel in Pane's dialog leaves the profile without it, on
	// its list, on Chrome's page, and on disk.
	await app.send("installExtension", { id: DARK_READER, profile });
	const offered = await waitForSheet(app, "extensionInstall", 60_000);

	expect(
		offered.title === "Add “Dark Reader” to Pane?" &&
			JSON.stringify(offered.buttons) ===
				JSON.stringify(["Add to Pane", "Cancel"]),
		`the install dialog: ${JSON.stringify(offered)}`,
	);

	await answerSheet(app, offered.buttons?.[1] ?? "Cancel");

	await app.waitFor(
		"the install dialog gone",
		(state) => state.sheet?.kind !== "extensionInstall",
	);

	const extensionDirectory = join(
		app.dataDir,
		ENGINE_FOLDER,
		profile,
		"Extensions",
		DARK_READER,
	);

	// Long enough for an install the answer did not stop to land.
	await Bun.sleep(2_000);
	const afterDecline = await app.state();

	expect(
		item(afterDecline) === undefined &&
			!existsSync(extensionDirectory) &&
			!(await listed(opened.page)).some((each) => each.id === DARK_READER),
		`declined, yet installed: ${JSON.stringify({ listed: item(afterDecline), onDisk: existsSync(extensionDirectory) })}`,
	);

	// Accepted: installed, and pinned (Chromium's default since 157), so its
	// button shows in the toolbar.
	const installed = await installById(app, profile, DARK_READER);

	expect(
		item(installed)?.pinned === true,
		`installed unpinned: ${JSON.stringify(item(installed))}`,
	);

	// Its welcome tab takes the window; Manage all extensions comes back to
	// the page's own tab.
	await app
		.waitFor(
			"Dark Reader's help tab",
			(state) =>
				(state.profiles.find((each) => each.id === profile)?.tabs ?? []).some(
					(each) => each.url.startsWith("https://darkreader.org/"),
				),
			30_000,
		)
		.catch(() => {});

	const again = await manage();

	expect(
		again.tab === opened.tab,
		`Manage all extensions opened ${again.tab}, not the page's tab ${opened.tab}`,
	);

	const page = again.page;

	const card = `${ITEMS}.querySelector("extensions-item#${DARK_READER}")`;

	await poll(
		"Dark Reader on Chrome's page, on",
		async () =>
			(await listed(page)).find((each) => each.id === DARK_READER)?.on === true,
		10_000,
	);

	// Its switch, off and on.
	const toggleSwitch = async (on: boolean) => {
		await press(
			page,
			`${card}.shadowRoot.querySelector("#enableToggle")`,
			"Dark Reader's switch",
		);

		await app.waitFor(
			`Dark Reader ${on ? "on" : "off"}`,
			(state) => item(state)?.enabled === on,
		);
	};

	await toggleSwitch(false);
	await toggleSwitch(true);

	// Details' Pin to toolbar, off and on.
	await press(
		page,
		`${card}.shadowRoot.querySelector("#detailsButton")`,
		"Dark Reader's Details",
	);

	const details = await poll(
		"Dark Reader's Details",
		async () => {
			const shown = await detailsShown(page).catch(() => null);

			return shown?.name === "Dark Reader" && shown;
		},
		10_000,
	);

	// Pane has no incognito: no Allow in Incognito, which Dark Reader would
	// have in Chrome.
	expect(
		details.pinRow && details.pinned && !details.incognitoRow,
		`Dark Reader's Details: ${JSON.stringify(details)}`,
	);

	const pin = `${DETAILS}.querySelector("#pin-to-toolbar")`;

	await press(page, pin, "Pin to toolbar");
	await app.waitFor("unpinned", (state) => item(state)?.pinned === false);
	await press(page, pin, "Pin to toolbar");
	await app.waitFor("pinned", (state) => item(state)?.pinned === true);

	await press(
		page,
		`${DETAILS}.querySelector("#closeButton")`,
		"the back arrow",
	);

	// Remove from Pane, in the extensions menu's more menu, asks first in
	// Pane's popup, under its button.
	await app.send("select", { tab: fixture });
	await app.send("extensionsMenu", { control: "puzzle" });
	await app.waitFor("the extensions menu", (state) => state.extensionsMenu);
	await app.send("extensionsMenu", { control: "more", id: DARK_READER });

	await app.waitFor(
		"the more menu",
		(state) => state.extensionMoreMenu === DARK_READER,
	);

	await app.send("extensionsMenu", { control: "remove" });
	const removing = await waitForSheet(app, "extensionRemove");

	expect(
		removing.title === "Remove “Dark Reader”?" &&
			JSON.stringify(removing.buttons) === JSON.stringify(["Remove", "Cancel"]),
		`the remove popup: ${JSON.stringify(removing)}`,
	);

	await answerSheet(app, removing.buttons?.[0] ?? "Remove");

	await app.waitFor(
		"Dark Reader removed",
		(state) => item(state) === undefined,
		30_000,
	);

	// Chrome's page lists it no more.
	await app.send("select", { tab: again.tab });

	await poll(
		"Dark Reader gone from Chrome's page",
		async () => !(await listed(page)).some((each) => each.id === DARK_READER),
		10_000,
	);

	await watcher.sample();
	await watcher.stop();
};

export default extensionManagement;
