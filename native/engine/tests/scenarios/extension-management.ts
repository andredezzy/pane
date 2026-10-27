import { join } from "node:path";
import {
	clickButton,
	clickControl,
	isScreenLocked,
	mainWindowOf,
	typeIntoField,
	windowState,
} from "../lib/cua";
import type { AppState, ExtensionState } from "../lib/pane";
import { answerSheet, WindowWatcher, waitForSheet } from "../lib/surfaces";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

const DARK_READER = "eimadpbcbfnmbkopoojfekhnkhdbieeh";

// Settings › Extensions manages a profile's extensions: install by Web Store
// ID (pinned to the toolbar), disable and enable, unpin and pin, update, and
// remove after asking.
// On an unlocked screen each control is clicked with CUA; else the same
// controller calls run through the automation channel.
const extensionManagement: Scenario = async (context) => {
	const { app, has } = await launchTraced(context, "extension-management");
	const watcher = new WindowWatcher(app).start();
	const profile = await app.createProfile("Manage");
	const url = context.fixtures.url("light.html?manage");
	const tab = await app.openTab(profile, url);
	await app.page(tab, url);

	const locked = await isScreenLocked();
	const pid = app.browser.pid;

	const item = (state: AppState): ExtensionState | undefined =>
		(state.extensions[profile] ?? []).find((each) => each.id === DARK_READER);

	// Settings (⌘,), where Extensions is a section.
	const showSettings = async () => {
		if ((await app.state()).page !== "settings") {
			await app.send("key", { key: ",", modifiers: ["command"] });
		}

		await app.waitFor("Settings", (state) => state.page === "settings");
	};

	await showSettings();

	// Install by its Web Store ID, then accept the app's install sheet.
	if (locked) {
		await app.send("installExtension", { id: DARK_READER, profile });
	} else {
		const window = await mainWindowOf(pid);

		await typeIntoField(
			pid,
			window,
			"pane.settings.extensions.input",
			DARK_READER,
		);

		await clickButton(pid, window, "Install");
	}

	const sheet = await waitForSheet(app, "extensionInstall", 60_000);

	const accepted = await answerSheet(
		app,
		sheet.buttons?.[0] ?? "Add extension",
	);

	const installed = await app.waitFor(
		"Dark Reader installed",
		(state) => item(state)?.enabled === true,
		120_000,
	);

	// A new extension is pinned (Chromium's default since 157), so its button
	// shows in the toolbar as every extension's did in the Electron app.
	expect(
		item(installed)?.pinned === true,
		`installed unpinned: ${JSON.stringify(item(installed))}`,
	);

	// Its welcome tab took the window; back to Settings.
	await Bun.sleep(1_000);
	await showSettings();

	// Each switch: a CUA click in the extension's row, or the same call.
	const toggle = async (
		title: "Enabled" | "Pinned",
		command: string,
		value: boolean,
	) => {
		if (locked) {
			await app.send(command, {
				id: DARK_READER,
				profile,
				[title === "Enabled" ? "enabled" : "pinned"]: value,
			});
		} else {
			await clickControl(pid, await mainWindowOf(pid), {
				title,
				role: /AX(CheckBox|Switch)/,
			});
		}

		return app.waitFor(`${title} ${value}`, (state) =>
			title === "Enabled"
				? item(state)?.enabled === value
				: item(state)?.pinned === value,
		);
	};

	// The extension's card, for the design record.
	if (!locked) {
		await Bun.sleep(500);

		await windowState(
			pid,
			await mainWindowOf(pid),
			join(context.out, "extension-settings.png"),
		);
	}

	const disabled = await toggle("Enabled", "setExtensionEnabled", false);
	const enabled = await toggle("Enabled", "setExtensionEnabled", true);
	const unpinned = await toggle("Pinned", "setExtensionPinned", false);
	const pinned = await toggle("Pinned", "setExtensionPinned", true);

	// Update All asks the Web Store for newer versions.
	if (locked) {
		await app.send("updateExtensions", { profile });
	} else {
		await clickButton(pid, await mainWindowOf(pid), "Update all");
	}

	await app.waitFor("the update check", () =>
		has("PaneBrowserContext.updateExtensions"),
	);

	// Uninstall asks first, in the extension's removal popup.
	if (locked) {
		await app.send("uninstallExtension", { id: DARK_READER, profile });
	} else {
		const window = await mainWindowOf(pid);
		await clickButton(pid, window, "Uninstall");
		await waitForSheet(app, "extensionRemove");
		await Bun.sleep(500);

		await windowState(
			pid,
			window,
			join(context.out, "extension-uninstall.png"),
		);

		await clickButton(pid, window, "Remove", "pane.extensionPopup.accept");
	}

	const removed = await app.waitFor(
		"Dark Reader removed",
		(state) => item(state) === undefined,
		30_000,
	);

	await watcher.sample("removed");

	expect(
		has("PaneBrowserContext.setExtensionEnabled") &&
			has("PaneBrowserContext.setExtensionPinned") &&
			has("PaneBrowserContext.uninstallExtension"),
		"trace lacks the management calls",
	);

	const windows = await watcher.stop();

	expect(
		windows.others.length === 0,
		`Chrome windows appeared: ${JSON.stringify(windows.others)}`,
	);

	return {
		via: locked ? "automation" : "cua",
		installed: item(installed),
		accepted,
		disabled: item(disabled)?.enabled,
		enabled: item(enabled)?.enabled,
		unpinned: item(unpinned)?.pinned,
		pinned: item(pinned)?.pinned,
		removed: (removed.extensions[profile] ?? []).map((each) => each.id),
		windows,
	};
};

export default extensionManagement;
