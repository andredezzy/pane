import { join } from "node:path";
import {
	DARK_READER,
	installFromWebStore,
	openPopupFromToolbar,
	screenshotPopover,
} from "../lib/extensions";
import { pollValue } from "../lib/poll";
import { WindowWatcher } from "../lib/surfaces";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// Dark Reader from the Chrome Web Store, in profile A only, darkens a light
// page in A and not in B; its toolbar popup opens.
const darkReader: Scenario = async (context) => {
	const { app, has } = await launchTraced(context, "dark-reader");
	const watcher = new WindowWatcher(app).start();
	const a = await app.createProfile("A", "BLUE");
	const b = await app.createProfile("B", "ROSE");

	await installFromWebStore(app, a, DARK_READER);
	await watcher.sample();

	const urlA = context.fixtures.url("light.html?profile=a");
	const urlB = context.fixtures.url("light.html?profile=b");
	const { page: pageB } = await app.openPage(b, urlB);
	const { page: pageA } = await app.openPage(a, urlA);

	const settle = (page: typeof pageA, dark: boolean) =>
		pollValue(
			() => page.evaluate<number>("luminance()"),
			(value) => (dark ? value <= 0.3 : value >= 0.9),
			15_000,
			250,
		);

	const luminanceA = await settle(pageA, true);
	const luminanceB = await settle(pageB, false);
	expect(luminanceA < 0.3, `profile A's page stayed light (${luminanceA})`);
	expect(luminanceB > 0.9, `profile B's page darkened (${luminanceB})`);

	// Its toolbar button opens the popup in the app's popover, sized to the
	// popup's page.
	await openPopupFromToolbar(app, a, DARK_READER);
	await Bun.sleep(1_500);

	await screenshotPopover(
		app,
		DARK_READER,
		join(context.out, "dark-reader-popup.png"),
	);

	await watcher.sample();
	await app.send("closeExtensionPopup");

	expect(
		has("PaneBrowserContextDelegate.confirmExtensionInstall"),
		"trace lacks the install prompt",
	);

	expect(
		has("PaneBrowserContextDelegate.showExtensionPopup"),
		"trace lacks the popup",
	);

	await watcher.stop();
};

export default darkReader;
