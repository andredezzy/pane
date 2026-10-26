import { join } from "node:path";
import { popupPage, workerPage } from "../lib/extensions";
import { tabOf } from "../lib/pane";
import { pageResult, WindowWatcher } from "../lib/surfaces";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

const PROBE = join(import.meta.dir, "..", "pages", "extensions", "tabs-api");

type TabInfo = { id: number; url: string; windowId: number } | null;

// chrome.tabs resolves to Pane's tabs: "the active tab of the current window"
// is the selected Pane tab, from a popup and from a service worker;
// tabs.create opens a Pane tab in the same profile; windows.create opens a
// tab too, as in the Electron app.
const tabsApi: Scenario = async (context) => {
	const { app } = await launchTraced(context, "tabs-api", {
		args: [`--load-extension=${PROBE}`],
	});

	const watcher = new WindowWatcher(app).start();
	const profile = await app.createProfile("Extensions");
	const url = context.fixtures.url("nav-a.html?tabs-api");
	const tab = await app.openTab(profile, url);
	await app.page(tab, url);

	const loaded = await app.waitFor("the probe extension", (state) =>
		(state.extensions[profile] ?? []).some(
			(item) => item.name === "Pane tabs API probe",
		),
	);

	const id = (loaded.extensions[profile] ?? []).find(
		(item) => item.name === "Pane tabs API probe",
	)?.id as string;

	// From the popup, which also asks the worker.
	await app.send("runExtensionAction", { id });

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

	// From the worker directly: tabs.create, then windows.create.
	const worker = await workerPage(app, id, url);
	const created = context.fixtures.url("nav-b.html?created-by-extension");

	await worker.evaluate(
		`chrome.tabs.create({ url: ${JSON.stringify(created)} })`,
	);

	const withTab = await app.waitFor("the extension's tab", (state) =>
		(state.profiles.find((p) => p.id === profile)?.tabs ?? []).some(
			(t) => t.url === created,
		),
	);

	const windowed = context.fixtures.url("nav-b.html?window-by-extension");

	await worker.evaluate(
		`chrome.windows.create({ url: ${JSON.stringify(windowed)}, type: "popup" })`,
	);

	const withWindowTab = await app.waitFor(
		"the extension's window, as a tab",
		(state) =>
			(state.profiles.find((p) => p.id === profile)?.tabs ?? []).some(
				(t) => t.url === windowed,
			),
	);

	await watcher.sample("after windows.create");

	// tabs.update({ active: true }) selects that tab in the app.
	await worker.evaluate(
		`chrome.tabs.query({ url: ${JSON.stringify(url)} }).then(([found]) => chrome.tabs.update(found.id, { active: true }))`,
	);

	const reselected = await app.waitFor(
		"the first tab selected by the extension",
		(state) => state.activeTabID === tab,
	);

	const tabs =
		withWindowTab.profiles
			.find((p) => p.id === profile)
			?.tabs.map((t) => t.url) ?? [];

	const windows = await watcher.stop();

	expect(
		windows.others.length === 0,
		`Chrome windows appeared: ${JSON.stringify(windows.others)}`,
	);

	return {
		extension: id,
		fromPopup,
		activeTab: tabOf(withTab, tab),
		tabsAfterCreate: tabs,
		selectedByUpdate: tabOf(reselected, tab)?.url,
		windows,
	};
};

export default tabsApi;
