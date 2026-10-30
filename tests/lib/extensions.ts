// Helpers for the Phase 4 scenarios: installing from the Chrome Web Store
// through its own install button (Pane's engine labels it "Add to Pane") and
// the app's install sheet, and
// reaching an extension's popup or service worker over CDP.
import { writeFileSync } from "node:fs";
import { openPage, Page } from "./cdp";
import {
	activateWebAccessibility,
	canDriveUI,
	clickButton,
	mainWindowOf,
	windowState,
} from "./cua";
import type { AppState, PaneApp } from "./pane";
import { type Answer, answerSheet, waitForSheet } from "./surfaces";
import { targets, waitForTarget } from "./targets";

export type Installed = {
	id: string;
	name: string;
	sheet: NonNullable<AppState["sheet"]>;
	answer: Answer;
	addClick: "cua" | "gesture";
};

// Opens the item's Web Store page in a tab of `profile`, presses "Add to
// Pane", and accepts the app's install sheet.
export async function installFromWebStore(
	app: PaneApp,
	profile: string,
	id: string,
): Promise<Installed> {
	const tab = await app.openTab(
		profile,
		`https://chromewebstore.google.com/detail/${id}`,
	);

	const store = await waitForTarget(
		app.browser.cdp,
		`the Web Store page of ${id}`,
		(target) =>
			target.type === "page" &&
			target.url.includes(id) &&
			target.title.includes("Chrome Web Store"),
		60_000,
	);

	const page = await openPage(app.browser.cdp, "about:blank", store.targetId);

	const button =
		"[...document.querySelectorAll('button')].find((b) => /Add to Pane/i.test(b.innerText))";

	const deadline = Date.now() + 30_000;

	while (
		!(await page.evaluate<boolean>(`!!${button}`)) &&
		Date.now() < deadline
	) {
		await Bun.sleep(500);
	}

	// The real button: a CUA click on an unlocked screen; else a click with a
	// user gesture, which the Web Store's install API requires.
	let addClick: Installed["addClick"] = "gesture";

	if (!(await canDriveUI())) {
		await page.send("Runtime.evaluate", {
			expression: `${button}.click()`,
			userGesture: true,
		});
	} else {
		// Another extension's welcome tab may have opened over the store's.
		await app.send("select", { tab });
		await activateWebAccessibility(app.browser.pid);

		await clickButton(
			app.browser.pid,
			await mainWindowOf(app.browser.pid),
			"Add to Pane",
		);

		addClick = "cua";
	}

	const sheet = await waitForSheet(app, "extensionInstall", 60_000);
	const answer = await answerSheet(app, sheet.buttons?.[0] ?? "Add extension");

	const installed = await app.waitFor(
		`${id} installed`,
		(state) =>
			(state.extensions[profile] ?? []).some(
				(item) => item.id === id && item.enabled,
			),
		120_000,
	);

	await app.send("closeTab", { tab });

	const name =
		(installed.extensions[profile] ?? []).find((item) => item.id === id)
			?.name ?? id;

	return { id, name, sheet, answer, addClick };
}

// The open popup page of extension `id`.
export async function popupPage(app: PaneApp, id: string): Promise<Page> {
	const target = await waitForTarget(
		app.browser.cdp,
		`the popup of ${id}`,
		(info) =>
			info.url.startsWith(`chrome-extension://${id}/`) &&
			info.type !== "service_worker" &&
			info.type !== "background_page",
	);

	return openPage(app.browser.cdp, "about:blank", target.targetId);
}

export async function screenshotPopup(page: Page, file: string): Promise<void> {
	const { data } = await page.send<{ data: string }>("Page.captureScreenshot", {
		format: "png",
	});

	writeFileSync(file, Buffer.from(data, "base64"));
}

// Pins extension `id` to `profile`'s toolbar (what its Pin menu item does)
// and presses its toolbar button: a CUA click on an unlocked screen, else
// through the automation channel.
export async function pressToolbarAction(
	app: PaneApp,
	profile: string,
	id: string,
): Promise<"cua" | "automation"> {
	await app.send("setExtensionPinned", { profile, id, pinned: true });

	const pinned = await app.waitFor("the pinned action", (state) =>
		(state.extensions[profile] ?? []).some(
			(item) => item.id === id && item.pinned,
		),
	);

	if (!(await canDriveUI())) {
		await app.send("runExtensionAction", { id });

		return "automation";
	}

	const item = pinned.extensions[profile]?.find((each) => each.id === id);

	await clickButton(
		app.browser.pid,
		await mainWindowOf(app.browser.pid),
		item?.actionTitle || item?.name || id,
		`pane.extension.${id}`,
	);

	return "cua";
}

export type OpenedPopup = {
	popup: NonNullable<AppState["extensionPopup"]>;
	click: "cua" | "automation";
};

// Presses extension `id`'s toolbar button and waits for the popover with
// the popup sized to its page.
export async function openPopupFromToolbar(
	app: PaneApp,
	profile: string,
	id: string,
): Promise<OpenedPopup> {
	const click = await pressToolbarAction(app, profile, id);

	const shown = await app.waitFor(
		"the popup at its page's size",
		(state) =>
			state.extensionPopup?.open === true && state.extensionPopup.width > 25,
		30_000,
	);

	return {
		popup: shown.extensionPopup as OpenedPopup["popup"],
		click,
	};
}

// A screenshot of the popover with extension `id`'s popup: through CUA on
// an unlocked screen, the main window with the popover over it (cua-driver
// captures the popover's own window as a copy of the main one); else the
// popup page through CDP.
export async function screenshotPopover(
	app: PaneApp,
	id: string,
	file: string,
): Promise<"cua" | "cdp"> {
	if (await canDriveUI()) {
		await windowState(
			app.browser.pid,
			await mainWindowOf(app.browser.pid),
			file,
		);

		return "cua";
	}

	await screenshotPopup(await popupPage(app, id), file);

	return "cdp";
}

// A screenshot of the app's window through CUA on an unlocked screen, else
// of `page` through CDP.
export async function screenshotWindow(
	app: PaneApp,
	file: string,
	page: Page,
): Promise<"cua" | "cdp"> {
	if (await canDriveUI()) {
		await windowState(
			app.browser.pid,
			await mainWindowOf(app.browser.pid),
			file,
		);

		return "cua";
	}

	await screenshotPopup(page, file);

	return "cdp";
}

// The extension's service worker in the profile of `tabURL`'s tab, to call
// extension APIs as the worker. (Chrome also loads its startup profile, where
// an extension loaded from the command line runs a worker of its own.)
export async function workerPage(
	app: PaneApp,
	id: string,
	tabURL: string,
): Promise<Page> {
	const all = await targets(app.browser.cdp);

	const context = all.find(
		(info) => info.type === "page" && info.url === tabURL,
	)?.browserContextId;

	const target = await waitForTarget(
		app.browser.cdp,
		`the worker of ${id}`,
		(info) =>
			info.type === "service_worker" &&
			info.url.startsWith(`chrome-extension://${id}/`) &&
			info.browserContextId === context,
	);

	const { sessionId } = await app.browser.cdp.send<{ sessionId: string }>(
		"Target.attachToTarget",
		{
			targetId: target.targetId,
			flatten: true,
		},
	);

	return new Page(app.browser.cdp, sessionId, target.targetId);
}
