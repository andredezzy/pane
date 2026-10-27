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

// The Extensions page manages a profile's extensions: install by Web Store
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

	// The Extensions page, as the extensions menu's Manage all extensions
	// opens it (the extensions-menu scenario clicks that).
	const showExtensions = async () => {
		if ((await app.state()).page !== "extensions") {
			await app.send("extensionsMenu", { control: "manage" });
		}

		await app.waitFor(
			"the Extensions page",
			(state) => state.page === "extensions",
		);
	};

	await showExtensions();

	// Install by its Web Store ID, then accept the app's install sheet.
	if (locked) {
		await app.send("installExtension", { id: DARK_READER, profile });
	} else {
		const window = await mainWindowOf(pid);

		await typeIntoField(pid, window, "pane.extensions.input", DARK_READER);

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

	// Its welcome tab took the window; back to the Extensions page.
	await Bun.sleep(1_000);
	await showExtensions();

	// Its switch and its pin: a CUA click on its card, or the same call.
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
		} else if (title === "Enabled") {
			await clickControl(pid, await mainWindowOf(pid), {
				title,
				role: /AX(CheckBox|Switch)/,
			});
		} else {
			await clickButton(
				pid,
				await mainWindowOf(pid),
				value ? "Pin to toolbar" : "Unpin from toolbar",
				`pane.extensions.pin.${DARK_READER}`,
			);
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
			join(context.out, "extensions-page.png"),
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

	// Remove asks first, in the extension's removal popup.
	if (locked) {
		await app.send("uninstallExtension", { id: DARK_READER, profile });
	} else {
		const window = await mainWindowOf(pid);
		await clickButton(
			pid,
			window,
			"Remove",
			`pane.extensions.remove.${DARK_READER}`,
		);
		await waitForSheet(app, "extensionRemove");
		await Bun.sleep(500);

		await windowState(
			pid,
			window,
			join(context.out, "extension-uninstall.png"),
		);

		// The press removes it, and its uninstall page opens in a new tab
		// before the press returns: accessibility can give up waiting
		// (-25204) though the press took. The removal below is the check.
		await clickButton(
			pid,
			window,
			"Remove",
			"pane.extensionPopup.accept",
		).catch((error: unknown) => {
			if (!String(error).includes("-25204")) {
				throw error;
			}
		});
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
