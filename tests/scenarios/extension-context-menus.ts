import { join } from "node:path";
import { workerPage } from "../lib/extensions";
import type { MenuItemState } from "../lib/pane";
import { poll } from "../lib/poll";
import { clickAt, launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

const PROBE = join(
	import.meta.dir,
	"..",
	"pages",
	"extensions",
	"context-menus",
);

const NAME = "Pane context menus probe";

type Point = { x: number; y: number };

type Click = {
	id: string;
	checked: boolean | null;
	pageUrl: string | null;
	tabId: number | null;
};

type ActionMenu = { items: MenuItemState[]; siteAccess: MenuItemState[] };

// What an extension adds with chrome.contextMenus shows where Chrome shows
// it. A page's menu has the probe's two page items in a submenu under its
// name, before Inspect; choosing one runs onClicked with the page and its
// tab, and the checkbox shows its new state the next time. The pinned
// action's right-click menu reads the probe's action items, the checkbox
// checked; choosing one runs onClicked for the active tab. Its site access
// follows, as Chrome's: the probe asks for every site, so the submenu offers
// When You Click the Extension, On <site>, and On All Sites, the current one
// checked, and each choice changes the sites the probe has; on a page it
// can't run on, it reads Can’t Read or Change Site’s Data.
const extensionContextMenus: Scenario = async (context) => {
	const { app, has, waitForEntry } = await launchTraced(
		context,
		"extension-context-menus",
		{ args: [`--load-extension=${PROBE}`] },
	);

	const profile = await app.createProfile("Menus");
	const url = context.fixtures.url("context.html?extension-context-menus");
	const tab = await app.openTab(profile, url);
	const page = await app.page(tab, url);

	const loaded = await app.waitFor("the probe extension", (state) =>
		(state.extensions[profile] ?? []).some((item) => item.name === NAME),
	);

	const id = (loaded.extensions[profile] ?? []).find(
		(item) => item.name === NAME,
	)?.id as string;

	const worker = await workerPage(app, id, url);

	// The clicks onClicked has kept, once there are `count`.
	const clicks = (count: number) =>
		poll(`${count} clicks in the probe`, async () => {
			const kept = await worker.evaluate<Click[]>(
				`chrome.storage.session.get("clicks").then((stored) => stored.clicks ?? [])`,
			);

			return kept.length >= count && kept;
		});

	// The menu of a plain spot of the page: the probe's page context.
	const plain = await page.evaluate<Point>("centerOf('plain')");

	const pageMenu = async () => {
		const before = (await app.state()).contextMenusShown;

		await clickAt(
			(method, params) => page.send(method, params),
			plain.x,
			plain.y,
			"right",
		);

		const state = await app.waitFor(
			"the page's menu with the probe",
			(current) =>
				current.contextMenusShown > before &&
				(current.contextMenu ?? []).some((item) => item.title === NAME),
		);

		return state.contextMenu as MenuItemState[];
	};

	const probeItems = (menu: MenuItemState[]) =>
		menu.find((item) => item.title === NAME)?.submenu ?? [];

	const described = (items: MenuItemState[]) =>
		items
			.map(
				(item) =>
					`${item.title}${item.enabled ? "" : " (disabled)"}${item.checked ? " (checked)" : ""}`,
			)
			.join(", ");

	const first = await pageMenu();
	const titles = first.map((item) => item.title);

	expect(
		titles.indexOf("Reload") < titles.indexOf(NAME) &&
			titles.indexOf(NAME) === titles.indexOf("Inspect") - 1,
		`the probe's submenu is not between the page's items and Inspect: ${titles.join(", ")}`,
	);

	expect(
		described(probeItems(first)) === "Probe page item, Probe page check",
		`the probe's submenu holds ${described(probeItems(first))}`,
	);

	await app.send("chooseContextMenuItem", { title: "Probe page item" });

	const [clicked] = await clicks(1);

	expect(
		clicked?.id === "page-item" &&
			clicked.pageUrl === url &&
			typeof clicked.tabId === "number",
		`onClicked got ${JSON.stringify(clicked)}`,
	);

	await pageMenu();
	await app.send("chooseContextMenuItem", { title: "Probe page check" });

	const checked = (await clicks(2))[1];

	expect(
		checked?.id === "page-check" && checked.checked === true,
		`the checkbox's onClicked got ${JSON.stringify(checked)}`,
	);

	const third = await pageMenu();

	expect(
		described(probeItems(third)) ===
			"Probe page item, Probe page check (checked)",
		`the probe's submenu holds ${described(probeItems(third))} once checked`,
	);

	await app.send("chooseContextMenuItem", { title: "Probe page check" });
	await clicks(3);

	expect(
		has("PaneContextMenu.runExtensionItem"),
		"the engine ran no page item",
	);

	// The action's own items, as its right-click menu reads them.
	await app.send("setExtensionPinned", { profile, id, pinned: true });

	const listed = await app.send<ActionMenu>("extensionActionMenu", { id });

	expect(
		described(listed.result.items) ===
			"Probe action item, Probe action check (checked)",
		`the action's menu reads ${described(listed.result.items)}`,
	);

	await app.send("extensionActionMenu", { id, run: "Probe action item" });

	const action = (await clicks(4))[3];

	expect(
		action?.id === "action-item" &&
			action.pageUrl === null &&
			action.tabId === clicked.tabId,
		`the action item's onClicked got ${JSON.stringify(action)}, the page's tab ${clicked.tabId}`,
	);

	expect(has("PaneExtensionActionMenu.run"), "the engine ran no action item");

	// Its site access on the page, as the menu reads it; then each choice,
	// and the sites the probe has after it (chrome.permissions.getAll).
	const site = new URL(url).hostname;
	const submenu = `This Can Read and Change Site Data`;

	const siteAccess = async () => {
		const menu = (await app.send<ActionMenu>("extensionActionMenu", { id }))
			.result.siteAccess;

		expect(
			menu.length === 1 && menu[0]?.title === submenu,
			`the action's site access reads ${described(menu)}`,
		);

		return described(menu[0]?.submenu ?? []);
	};

	const origins = () =>
		worker.evaluate<string[]>(
			"chrome.permissions.getAll().then((all) => all.origins)",
		);

	const choices = (checked: string) =>
		[`When You Click the Extension`, `On ${site}`, `On All Sites`]
			.map((title) => (title === checked ? `${title} (checked)` : title))
			.concat("Learn More About Site Access")
			.join(", ");

	const initially = await siteAccess();

	expect(
		initially === choices("On All Sites"),
		`the site access submenu reads ${initially}`,
	);

	const choose = async (title: string, has: (origins: string[]) => boolean) => {
		await app.send("extensionActionMenu", { id, run: title });

		const after = await poll(`the probe's sites after ${title}`, async () => {
			const current = await origins();

			return has(current) && current;
		});

		const menu = await siteAccess();

		expect(
			menu === choices(title),
			`after ${title}, the submenu reads ${menu}`,
		);

		return { origins: after, menu };
	};

	const onClick = await choose(
		"When You Click the Extension",
		(current) => !current.includes("<all_urls>"),
	);

	// Chrome would ask to reload the page now; Pane declines its dialog, and
	// no window of Chrome's shows.
	await waitForEntry("PaneExtensions.declineReloadPrompt");

	const windows = (await app.state()).nativeWindows;

	expect(
		windows.every((window) => window.role === "main"),
		`a window showed with the main one: ${JSON.stringify(windows)}`,
	);

	const onSite = await choose(
		`On ${site}`,
		(current) =>
			!current.includes("<all_urls>") &&
			current.some(
				(origin) => new URL(origin.replace("*", "x")).hostname === site,
			),
	);

	const onAllSites = await choose("On All Sites", (current) =>
		current.includes("<all_urls>"),
	);

	expect(
		has("PaneExtensionActionMenu.run", (args) => args.title === `On ${site}`),
		"the engine ran no site access choice",
	);

	// A page it can't run on: data: is no scheme host permissions cover.
	const data = "data:text/plain,no-sites";
	const unreachable = await app.openTab(profile, data);

	await app.page(unreachable, data);

	const cantAccess = (await app.send<ActionMenu>("extensionActionMenu", { id }))
		.result.siteAccess;

	expect(
		described(cantAccess) ===
			"Can\u2019t Read or Change Site\u2019s Data (disabled)",
		`on a data: page, the site access reads ${described(cantAccess)}`,
	);

	return {
		extension: id,
		pageMenu: titles,
		probeSubmenu: described(probeItems(first)),
		actionMenu: described(listed.result.items),
		siteAccess: {
			initially,
			onClick,
			onSite,
			onAllSites,
			cantAccess: described(cantAccess),
		},
		clicks: await clicks(4),
	};
};

export default extensionContextMenus;
