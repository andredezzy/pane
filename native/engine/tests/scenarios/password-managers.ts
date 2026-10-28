import { join } from "node:path";
import type { Page } from "../lib/cdp";
import {
	installFromWebStore,
	popupPage,
	pressToolbarAction,
	screenshotPopover,
	screenshotWindow,
} from "../lib/extensions";
import { tabOf } from "../lib/pane";
import { WindowWatcher } from "../lib/surfaces";
import { waitForPage } from "../lib/targets";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

const MANAGERS = [
	{ id: "aeblfdkhhhdcdjpifhhbdiojplfjncoa", file: "1password.png" },
	{ id: "eiaeiblijfjekdanodkjadfinkhbfgcd", file: "nordpass.png" },
];

// The first words a page shows: it may show only a logo while it starts.
async function firstWords(page: Page): Promise<string> {
	const deadline = Date.now() + 30_000;
	let text = "";

	while (!text && Date.now() < deadline) {
		text = (
			await page.evaluate<string>(
				"(document.body?.innerText ?? '').trim().slice(0, 200)",
			)
		).trim();

		if (!text) {
			await Bun.sleep(500);
		}
	}

	return text;
}

// 1Password and NordPass from the Chrome Web Store: their toolbar buttons
// work in Pane. A manager with a popup shows it in the app's popover; one
// that turned its popup off until the user signs in (1Password does, seconds
// after installing) opens its own page in a tab instead, as in Chrome.
// Signing in stays with the user (HUMAN-CHECKLIST).
const passwordManagers: Scenario = async (context) => {
	const { app } = await launchTraced(context, "password-managers");
	const watcher = new WindowWatcher(app).start();
	const profile = await app.createProfile("Passwords");
	const url = context.fixtures.url("password.html?managers");
	const results: Record<string, unknown>[] = [];

	for (const manager of MANAGERS) {
		const installed = await installFromWebStore(app, profile, manager.id);

		// Each may open a welcome tab after ours; the button is pressed on
		// our page, selected again once no more tabs open.
		const tab = await app.openTab(profile, `${url}&${manager.id}`);
		await app.page(tab, `${url}&${manager.id}`);

		await app.settled(
			`${installed.name}'s tabs`,
			(state) =>
				state.profiles
					.find((each) => each.id === profile)
					?.tabs.map((each) => each.id),
			2_000,
		);

		await app.send("select", { tab });

		const file = join(context.out, manager.file);

		await app.waitFor(`${installed.name}'s toolbar icon`, (state) =>
			(state.extensions[profile] ?? []).some(
				(item) => item.id === manager.id && item.iconVisible,
			),
		);

		// Whether it has a popup is known once its button is pressed: one
		// turns it off seconds after installing (1Password).
		const click = await pressToolbarAction(app, profile, manager.id);
		const own = `chrome-extension://${manager.id}/`;

		const pressed = await app.waitFor(
			`${installed.name}'s popup or its own page`,
			(state) =>
				(state.extensionPopup?.open === true &&
					state.extensionPopup.width > 25) ||
				(tabOf(state, state.activeTabID ?? "")?.url.startsWith(own) === true &&
					state.activeTabID !== tab),
			30_000,
		);

		if (pressed.extensionPopup?.open) {
			const opened = { popup: pressed.extensionPopup, click };
			const text = await firstWords(await popupPage(app, manager.id));
			await Bun.sleep(1_000);
			const shot = await screenshotPopover(app, manager.id, file);
			await watcher.sample(manager.file);
			await app.send("closeExtensionPopup");

			expect(text.length > 0, `${installed.name}'s popup rendered nothing`);

			results.push({
				installed,
				opens: "popup",
				popup: opened.popup,
				toolbarClick: opened.click,
				text,
				screenshot: { file: manager.file, via: shot },
			});

			continue;
		}

		const opened = tabOf(pressed, pressed.activeTabID ?? "");

		// Its page routes itself (the URL changes as it starts).
		const page = await waitForPage(
			app.browser.cdp,
			`${installed.name}'s page`,
			(info) => info.url.startsWith(own),
		);

		const text = await firstWords(page);
		await Bun.sleep(1_000);
		const shot = await screenshotWindow(app, file, page);
		await watcher.sample(manager.file);

		expect(text.length > 0, `${installed.name}'s page rendered nothing`);

		results.push({
			installed,
			opens: "tab",
			page: opened?.url,
			toolbarClick: click,
			text,
			screenshot: { file: manager.file, via: shot },
		});
	}

	const windows = await watcher.stop();

	expect(
		windows.others.length === 0,
		`Chrome windows appeared: ${JSON.stringify(windows.others)}`,
	);

	return { managers: results, windows };
};

export default passwordManagers;
