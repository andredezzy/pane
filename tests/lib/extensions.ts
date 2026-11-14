// Installing from the Chrome Web Store through its own install button (Pane's
// engine labels it "Add to Pane") and the app's install sheet, and reaching an
// extension's popup or service worker over CDP.
import { writeFileSync } from "node:fs";
import { basename } from "node:path";
import { openPage, Page } from "./cdp";
import { canDriveUI, clickButton, mainWindowOf } from "./computer-use";
import type { AppState, PaneApp } from "./pane";
import { poll } from "./poll";
import { answerSheet, waitForSheet } from "./surfaces";
import { targets, waitForTarget } from "./targets";
import { captureWindow } from "./tools";
import { activateWebAccessibility } from "./windows";

export const DARK_READER = "eimadpbcbfnmbkopoojfekhnkhdbieeh";

// The id of the extension called `name`, once `profile` lists it.
export async function extensionId(
	app: PaneApp,
	profile: string,
	name: string,
): Promise<string> {
	const state = await app.waitFor(`the extension ${name}`, (current) =>
		(current.extensions[profile] ?? []).some((item) => item.name === name),
	);

	return (state.extensions[profile] ?? []).find((item) => item.name === name)
		?.id as string;
}

// Installs by its Web Store id, as the store's own button does, and accepts
// the app's install sheet: the state once it is enabled.
export async function installById(
	app: PaneApp,
	profile: string,
	id: string,
): Promise<AppState> {
	await app.send("installExtension", { id, profile });
	const sheet = await waitForSheet(app, "extensionInstall", 60_000);
	await answerSheet(app, sheet.buttons?.[0] ?? "Add extension");

	return app.waitFor(
		`${id} installed`,
		(state) =>
			(state.extensions[profile] ?? []).some(
				(item) => item.id === id && item.enabled,
			),
		120_000,
	);
}

// Opens the item's Web Store page in a tab of `profile`, presses "Add to
// Pane", and accepts Pane's dialog: the extension's name. With the front
// (`foreground`), the button takes a real click through accessibility;
// otherwise a click with a user gesture, in the page, which no other app
// sees.
export async function installFromWebStore(
	app: PaneApp,
	profile: string,
	id: string,
	foreground = false,
): Promise<string> {
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

	await poll(
		`the Add to Pane button of ${id}`,
		() => page.evaluate<boolean>(`!!${button}`),
		30_000,
		500,
	);

	// The real button: a CUA click with the front on an unlocked screen; else
	// a click with a user gesture, which the Web Store's install API requires.
	const press = async () => {
		if (!foreground || !(await canDriveUI())) {
			await page.gesture(`${button}.click()`);

			return;
		}

		// Another extension's welcome tab may have opened over the store's.
		await app.send("select", { tab });
		await activateWebAccessibility(app.browser.pid);

		await clickButton(
			app.browser.pid,
			await mainWindowOf(app.browser.pid),
			"Add to Pane",
		);
	};

	// The store wires its button a moment after it shows, and a click before
	// that does nothing: the scenarios clicked once, at once, and waited out
	// the minute. The click repeats until the install sheet comes.
	let sheet: NonNullable<AppState["sheet"]> | undefined;

	for (const until = Date.now() + 60_000; !sheet && Date.now() < until; ) {
		if ((await app.state()).sheet?.kind !== "extensionInstall") {
			await press();
		}

		sheet = await waitForSheet(app, "extensionInstall", 3_000).catch(
			() => undefined,
		);
	}

	if (!sheet) {
		throw new Error(`no install sheet for ${id} after a minute of clicks`);
	}

	await answerSheet(app, sheet.buttons?.[0] ?? "Add extension");

	const installed = await app.waitFor(
		`${id} installed`,
		(state) =>
			(state.extensions[profile] ?? []).some(
				(item) => item.id === id && item.enabled,
			),
		120_000,
	);

	await app.send("closeTab", { tab });

	return (
		(installed.extensions[profile] ?? []).find((item) => item.id === id)
			?.name ?? id
	);
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

async function screenshotPopup(page: Page, file: string): Promise<void> {
	const { data } = await page.send<{ data: string }>("Page.captureScreenshot", {
		format: "png",
	});

	writeFileSync(file, Buffer.from(data, "base64"));
}

// Pins extension `id` to `profile`'s toolbar (what its Pin menu item does)
// and presses its toolbar button: a real click at its middle, through the
// channel, in the background too.
export async function pressToolbarAction(
	app: PaneApp,
	profile: string,
	id: string,
): Promise<void> {
	await app.send("setExtensionPinned", { profile, id, pinned: true });

	await app.waitFor("the pinned action", (state) =>
		(state.extensions[profile] ?? []).some(
			(item) => item.id === id && item.pinned,
		),
	);

	await app.click(`pane.extension.${id}`);
}

// Presses extension `id`'s toolbar button and waits for the popover with
// the popup sized to its page.
export async function openPopupFromToolbar(
	app: PaneApp,
	profile: string,
	id: string,
): Promise<void> {
	await pressToolbarAction(app, profile, id);

	await app.waitFor(
		"the popup at its page's size",
		(state) =>
			state.extensionPopup?.open === true && state.extensionPopup.width > 25,
		30_000,
	);
}

// A screenshot of the popup of extension `id`: its bubble as the app
// renders it, and its page through CDP beside it.
export async function screenshotPopover(
	app: PaneApp,
	id: string,
	file: string,
): Promise<void> {
	await app.renderPopup(basename(file).replace(/\.png$/, "-bubble.png"));
	await screenshotPopup(await popupPage(app, id), file);
}

// A screenshot of the app's window on an unlocked screen, else of `page`
// through CDP.
export async function screenshotWindow(
	app: PaneApp,
	file: string,
	page: Page,
): Promise<void> {
	if (await canDriveUI()) {
		await captureWindow(await mainWindowOf(app.browser.pid), file);
	} else {
		await screenshotPopup(page, file);
	}
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

	const worker = new Page(app.browser.cdp, sessionId, target.targetId);

	// A worker just started can take an evaluation before its extension's
	// API is bound to it ("chrome is not defined", views-extensions in dark
	// on 2026-11-11), and answers once it is.
	await poll(
		`the worker of ${id} with its API`,
		() =>
			worker
				.evaluate<boolean>("typeof chrome === 'object' && !!chrome.runtime?.id")
				.catch(() => false),
		10_000,
	);

	return worker;
}
