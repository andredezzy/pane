import { join } from "node:path";
import { extensionId, popupPage, workerPage } from "../lib/extensions";
import { bringToFront } from "../lib/pane";
import { pageResult, WindowWatcher } from "../lib/surfaces";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

const PROBE = join(import.meta.dir, "..", "pages", "extensions", "tabs-api");

type TabInfo = { id: number; url: string; windowId: number } | null;

// chrome.tabs resolves to Pane's tabs: "the active tab of the current window"
// is the selected Pane tab, from a popup and from a service worker;
// tabs.create opens a Pane tab in the same profile; windows.create opens a
// tab too, as in the Electron app. chrome.action.openPopup shows the popup in
// the app's bubble, as the extension's button does. A popup open as its
// profile is deleted closes with the profile's Browser. A build with no
// extension popup yet (the Views line before X2) asks the worker alone, and
// defers the rest.
const tabsApi: Scenario = async (context) => {
	const { app, trace } = await launchTraced(context, "tabs-api", {
		args: [`--load-extension=${PROBE}`],
	});

	const watcher = new WindowWatcher(app).start();
	const profile = await app.createProfile("Extensions");
	const url = context.fixtures.url("nav-a.html?tabs-api");
	const { tab } = await app.openPage(profile, url);

	const id = await extensionId(app, profile, "Pane tabs API probe");
	const worker = await workerPage(app, id, url);

	// From the popup, which also asks the worker.
	const hasPopups = await app.send("runExtensionAction", { id }).then(
		() => true,
		(error: Error) => {
			if (!error.message.includes("unknown command")) {
				throw error;
			}

			context.defer(
				"the popup's view of the active tab, chrome.action.openPopup, and a popup closing with its profile: the extension's popup waits for X2",
			);

			return false;
		},
	);

	if (hasPopups) {
		await app.waitFor(
			"the popup",
			(state) => state.extensionPopup?.open === true,
		);

		const popup = await popupPage(app, id);

		const fromPopup = await pageResult<{ popup: TabInfo; worker: TabInfo }>(
			(expression) => popup.evaluate(expression),
		);

		await app.send("closeExtensionPopup");

		expect(
			fromPopup.popup?.url === url,
			`popup saw ${JSON.stringify(fromPopup.popup)}`,
		);

		expect(
			fromPopup.worker?.url === url,
			`worker saw ${JSON.stringify(fromPopup.worker)}`,
		);
	} else {
		const fromWorker = await worker.evaluate<TabInfo>(
			`chrome.tabs.query({ active: true, currentWindow: true }).then(([tab]) => tab ? { id: tab.id, url: tab.url, windowId: tab.windowId } : null)`,
		);

		expect(fromWorker?.url === url, `worker saw ${JSON.stringify(fromWorker)}`);
	}

	// From the worker directly: tabs.create, then windows.create.
	const created = context.fixtures.url("nav-b.html?created-by-extension");

	await worker.evaluate(
		`chrome.tabs.create({ url: ${JSON.stringify(created)} })`,
	);

	await app.waitFor("the extension's tab", (state) =>
		(state.profiles.find((p) => p.id === profile)?.tabs ?? []).some(
			(t) => t.url === created,
		),
	);

	const windowed = context.fixtures.url("nav-b.html?window-by-extension");

	await worker.evaluate(
		`chrome.windows.create({ url: ${JSON.stringify(windowed)}, type: "popup" })`,
	);

	await app.waitFor("the extension's window, as a tab", (state) =>
		(state.profiles.find((p) => p.id === profile)?.tabs ?? []).some(
			(t) => t.url === windowed,
		),
	);

	await watcher.sample();

	// tabs.update({ active: true }) selects that tab in the app.
	await worker.evaluate(
		`chrome.tabs.query({ url: ${JSON.stringify(url)} }).then(([found]) => chrome.tabs.update(found.id, { active: true }))`,
	);

	await app.waitFor(
		"the first tab selected by the extension",
		(state) => state.activeTabID === tab,
	);

	if (!hasPopups) {
		await watcher.stop();

		return;
	}

	// chrome.action.openPopup, which asks for an active window: the app's,
	// brought forward. Its popup opens where the toolbar button's does,
	// through the ExtensionsContainer Pane registers for each Browser it
	// hosts, as Chrome's windows register theirs.
	const popups = () =>
		trace().filter((entry) => entry.name === "ExtensionPopup.show").length;

	const popupsBefore = popups();

	if (await bringToFront(context, app, "chrome.action.openPopup")) {
		await worker.evaluate("chrome.action.openPopup()");

		await app.waitFor(
			"the popup chrome.action.openPopup opened",
			(state) =>
				state.extensionPopup?.extension === id &&
				state.extensionPopup.open === true,
		);

		expect(
			popups() === popupsBefore + 1,
			`the app was asked to show ${popups() - popupsBefore} popups`,
		);

		await app.send("closeExtensionPopup");

		await app.waitFor(
			"the popup closed",
			(state) => state.extensionPopup === null,
		);
	}

	// A popup still open as its profile goes (deleted, which closes its
	// Browser) closes first: the popup's page holds that Browser.
	await app.send("runExtensionAction", { id });

	await app.waitFor(
		"the popup, again",
		(state) => state.extensionPopup?.open === true,
	);

	const closes = () =>
		trace().filter(
			(entry) =>
				entry.name === "ExtensionPopup.closeWithBrowser" ||
				entry.name === "ExtensionPopup.close",
		).length;

	const closesBefore = closes();

	await app.send("deleteProfile", { profile });

	await app.waitFor(
		"the popup gone with its profile",
		(state) =>
			state.extensionPopup === null && state.pendingProfileDeletions === 0,
	);

	// It closed as its profile's tabs went, before their Browser: as its
	// active tab changed, as Chrome's popup closes, else with the Browser.
	expect(closes() > closesBefore, "the popup outlived its profile's Browser");

	await watcher.stop();
};

export default tabsApi;
