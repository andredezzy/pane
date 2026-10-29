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

// What an extension adds with chrome.contextMenus shows where Chrome shows
// it. A page's menu has the probe's two page items in a submenu under its
// name, before Inspect; choosing one runs onClicked with the page and its
// tab, and the checkbox shows its new state the next time. The pinned
// action's right-click menu reads the probe's action items, the checkbox
// checked; choosing one runs onClicked for the active tab.
const extensionContextMenus: Scenario = async (context) => {
	const { app, has } = await launchTraced(context, "extension-context-menus", {
		args: [`--load-extension=${PROBE}`],
	});

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

	const listed = await app.send<MenuItemState[]>("extensionActionMenu", { id });

	expect(
		described(listed.result) ===
			"Probe action item, Probe action check (checked)",
		`the action's menu reads ${described(listed.result)}`,
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

	return {
		extension: id,
		pageMenu: titles,
		probeSubmenu: described(probeItems(first)),
		actionMenu: described(listed.result),
		clicks: await clicks(4),
	};
};

export default extensionContextMenus;
