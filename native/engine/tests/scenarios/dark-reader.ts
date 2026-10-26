import { join } from "node:path";
import {
	installFromWebStore,
	openPopupFromToolbar,
	screenshotPopover,
} from "../lib/extensions";
import { WindowWatcher } from "../lib/surfaces";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

const DARK_READER = "eimadpbcbfnmbkopoojfekhnkhdbieeh";

// Dark Reader from the Chrome Web Store, in profile A only, darkens a light
// page in A and not in B; its toolbar popup opens.
const darkReader: Scenario = async (context) => {
	const { app, has } = await launchTraced(context, "dark-reader");
	const watcher = new WindowWatcher(app).start();
	const a = await app.createProfile("A", "BLUE");
	const b = await app.createProfile("B", "ROSE");

	const installed = await installFromWebStore(app, a, DARK_READER);
	await watcher.sample("installed");

	const urlA = context.fixtures.url("light.html?profile=a");
	const urlB = context.fixtures.url("light.html?profile=b");
	const tabB = await app.openTab(b, urlB);
	const pageB = await app.page(tabB, urlB);
	const tabA = await app.openTab(a, urlA);
	const pageA = await app.page(tabA, urlA);

	const settle = async (page: typeof pageA, dark: boolean) => {
		const deadline = Date.now() + 15_000;
		let value = await page.evaluate<number>("luminance()");

		while ((dark ? value > 0.3 : value < 0.9) && Date.now() < deadline) {
			await Bun.sleep(250);
			value = await page.evaluate<number>("luminance()");
		}

		return value;
	};

	const luminanceA = await settle(pageA, true);
	const luminanceB = await settle(pageB, false);
	expect(luminanceA < 0.3, `profile A's page stayed light (${luminanceA})`);
	expect(luminanceB > 0.9, `profile B's page darkened (${luminanceB})`);

	// Its toolbar button opens the popup in the app's popover, sized to the
	// popup's page.
	const opened = await openPopupFromToolbar(app, a, DARK_READER);
	await Bun.sleep(1_500);

	const popupShot = await screenshotPopover(
		app,
		DARK_READER,
		join(context.out, "dark-reader-popup.png"),
	);

	await watcher.sample("popup");
	await app.send("closeExtensionPopup");

	expect(
		has("PaneBrowserContextDelegate.confirmExtensionInstall"),
		"trace lacks the install prompt",
	);

	expect(
		has("PaneBrowserContextDelegate.showExtensionPopup"),
		"trace lacks the popup",
	);

	const windows = await watcher.stop();

	expect(
		windows.others.length === 0,
		`Chrome windows appeared: ${JSON.stringify(windows.others)}`,
	);

	return {
		installed,
		luminance: { a: luminanceA, b: luminanceB },
		popup: opened.popup,
		toolbarClick: opened.click,
		popupScreenshot: { file: "dark-reader-popup.png", via: popupShot },
		windows,
	};
};

export default darkReader;
