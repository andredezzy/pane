import { join } from "node:path";
import {
	detailsURL,
	EXTENSIONS_PAGE,
	selectedAt,
} from "../lib/chrome-extensions";
import { extensionId } from "../lib/extensions";
import {
	type AppState,
	bringToFront,
	launchPane,
	type PaneApp,
} from "../lib/pane";
import { pollValue } from "../lib/poll";
import { waitForSheet } from "../lib/surfaces";
import { expect, type Scenario, type ScenarioContext } from "../run";

const PROBE = join(
	import.meta.dir,
	"..",
	"pages",
	"extensions",
	"action-probe",
);

const NAME = "Pane action probe";

// The toolbar's puzzle opens the extensions menu, as Aside's (Paper 03.9
// Browser · Extensions, 04.6; ADR 0054): Manage all extensions opens
// Chrome's extensions page in the profile's tab (D3), Visit Chrome Web Store
// opens the store in a new tab, and each extension's row runs its action,
// its pin pins and unpins it with the menu still open, and its more menu
// opens beside it, where Manage Extension opens the extension's Details in
// that same tab and Remove from Pane asks first, in the popup under the
// extension's button (ADR 0056), with Report abuse. Esc and a click outside
// close the menu. Each control is a real click at its view's middle through
// the channel (extensionsMenu), in the background too; Esc is Views' own
// dispatch there, a real key with Pane in front. The extension is a local
// probe with an icon, a popup, and options, loaded from the command line.
async function press(
	app: PaneApp,
	control: string,
	id?: string,
): Promise<void> {
	await app.send("extensionsMenu", { control, id });
}

async function pressEscape(
	context: ScenarioContext,
	app: PaneApp,
): Promise<void> {
	if (context.foreground) {
		await app.send("key", { key: "\u001b" });
	} else {
		await app.send("dispatchKey", { key: "\u001b" });
	}
}

async function openMenu(app: PaneApp): Promise<AppState> {
	await press(app, "puzzle");

	return app.waitFor("the extensions menu", (state) => state.extensionsMenu);
}

const extensionsMenu: Scenario = async (context) => {
	const app = await launchPane(context, {
		args: [`--load-extension=${PROBE}`],
	});

	// Allowed the front, it takes it now: asked for only once the run is under way,
	// it doesn't always win it from the app in use.
	if (context.foreground) {
		await bringToFront(context, app, "the run");
	}

	const profile = await app.createProfile("Extensions");
	const url = context.fixtures.url("light.html?extensions-menu");
	const { tab } = await app.openPage(profile, url);
	const id = await extensionId(app, profile, NAME);

	const item = (state: AppState) =>
		(state.extensions[profile] ?? []).find((each) => each.id === id);

	// None on: the puzzle still shows, and the menu says so.
	await app.send("setExtensionEnabled", { id, profile, enabled: false });
	await app.waitFor("the probe off", (state) => item(state)?.enabled === false);
	await openMenu(app);
	await app.render("menu-empty.png");

	const empty = await app.frames("pane.extensionsMenu");

	expect(
		!Object.keys(empty).some((name) =>
			name.startsWith("pane.extensionsMenu.extension."),
		),
		`the empty menu lists ${JSON.stringify(Object.keys(empty))}`,
	);

	// Esc closes it.
	await pressEscape(context, app);
	await app.waitFor("the menu closed by Esc", (state) => !state.extensionsMenu);

	// Visit Chrome Web Store: a new tab in the profile, and the menu closes.
	await openMenu(app);
	await press(app, "webStore");

	const store = await app.waitFor(
		"the Web Store tab",
		(state) =>
			!state.extensionsMenu &&
			(state.profiles.find((each) => each.id === profile)?.tabs ?? []).some(
				(each) => each.url.startsWith("https://chromewebstore.google.com/"),
			),
	);

	const storeTab = (
		store.profiles.find((each) => each.id === profile)?.tabs ?? []
	).find((each) => each.url.startsWith("https://chromewebstore.google.com/"));

	if (storeTab) {
		await app.send("closeTab", { tab: storeTab.id });
	}

	// Manage all extensions: Chrome's extensions page in the profile's tab.
	await app.send("select", { tab });
	await openMenu(app);
	await press(app, "manage");
	const { tab: pageTab } = await selectedAt(app, profile, EXTENSIONS_PAGE);
	expect(pageTab !== tab, "Manage all extensions replaced the page's tab");

	// The probe on and pinned: its button in the toolbar, in the capsule.
	await app.send("select", { tab });
	await app.send("setExtensionEnabled", { id, profile, enabled: true });
	await app.send("setExtensionPinned", { id, profile, pinned: true });

	await app.waitFor(
		"the probe pinned",
		(state) => item(state)?.enabled === true && item(state)?.pinned === true,
		30_000,
	);

	// The toolbar lays it out at its next frame, after the state says pinned.
	const toolbar = await pollValue(
		() => app.frames("pane.extension"),
		(frames) => frames[`pane.extension.${id}`]?.[2] === 32,
		5_000,
	);

	expect(
		(toolbar[`pane.extension.${id}`]?.[2] ?? 0) === 32,
		`the pinned action's button: ${JSON.stringify(toolbar)}`,
	);

	// Its pin unpins it and pins it again, the menu open throughout.
	await openMenu(app);
	await press(app, "pin", id);

	await app.waitFor(
		"unpinned from the menu",
		(state) => item(state)?.pinned === false && state.extensionsMenu,
	);

	await press(app, "pin", id);

	await app.waitFor(
		"pinned again from the menu",
		(state) => item(state)?.pinned === true && state.extensionsMenu,
	);

	await app.render("menu-light.png");

	// Its row runs its action: the popup, and the menu closes.
	await press(app, "run", id);

	const shown = await app.waitFor(
		"its popup",
		(state) =>
			state.extensionPopup?.extension === id &&
			state.extensionPopup.open &&
			!state.extensionsMenu,
		30_000,
	);

	expect(
		shown.extensionPopup?.width === 300 && shown.extensionPopup.height === 180,
		`the popup's page sized itself to ${JSON.stringify(shown.extensionPopup)}`,
	);

	await app.renderPopup("popup-light.png", 500);
	await app.send("closeExtensionPopup");

	await app.waitFor(
		"the popup closed",
		(state) => state.extensionPopup === null,
	);

	// More: Pane's own menu beside the row, its Options as the probe has a
	// page; Manage Extension opens the probe's Details, in the page's tab.
	await openMenu(app);
	await press(app, "more", id);

	await app.waitFor("the more menu", (state) => state.extensionMoreMenu === id);

	const more = await app.frames("pane.extensionMoreMenu");

	expect(
		[
			"pane.extensionMoreMenu.storePage",
			"pane.extensionMoreMenu.remove",
			"pane.extensionMoreMenu.pin",
			"pane.extensionMoreMenu.manage",
			"pane.extensionMoreMenu.options",
		].every((name) => name in more),
		`the more menu's rows: ${JSON.stringify(Object.keys(more))}`,
	);

	await app.render("more-menu.png");

	// Esc closes the more menu first, then the menu.
	await pressEscape(context, app);

	await app.waitFor(
		"Esc: the more menu closed, the menu still open",
		(state) => state.extensionMoreMenu === null && state.extensionsMenu,
	);

	await press(app, "more", id);

	await app.waitFor(
		"the more menu again",
		(state) => state.extensionMoreMenu === id,
	);

	await press(app, "manageExtension");
	const details = await selectedAt(app, profile, detailsURL(id));

	expect(
		details.tab === pageTab &&
			!details.state.extensionsMenu &&
			details.state.extensionMoreMenu === null,
		`Manage Extension opened ${details.tab}, the page's tab is ${pageTab}`,
	);

	// The Dark theme: the menu and the popup.
	await app.send("theme", { theme: "dark" });
	await app.send("select", { tab });
	const opened = await openMenu(app);
	await app.render("menu-dark.png");

	// A click outside closes it, and goes nowhere else: the page under it
	// keeps its tab.
	const height = Number(opened.window.frame.match(/([\d.]+)\}\}$/)?.[1] ?? 800);
	await app.send("clickWindow", { x: 600, y: height - 100 });

	await app.waitFor(
		"the menu closed by a click outside",
		(state) => !state.extensionsMenu,
	);

	await app.send("runExtensionAction", { id });

	await app.waitFor(
		"its popup, dark",
		(state) => state.extensionPopup?.open === true,
		30_000,
	);

	await app.renderPopup("popup-dark.png", 500);
	await app.send("closeExtensionPopup");

	await app.waitFor(
		"the popup closed",
		(state) => state.extensionPopup === null,
	);

	// More › Remove from Pane asks first, in the popup under the probe's
	// button: Report abuse ticked, then Cancel, keeps it, and opens nothing.
	const remove = async () => {
		await openMenu(app);
		await press(app, "more", id);

		await app.waitFor(
			"the more menu",
			(state) => state.extensionMoreMenu === id,
		);

		await press(app, "remove");

		return waitForSheet(app, "extensionRemove");
	};

	const asked = await remove();
	const name = item(await app.state())?.name ?? NAME;

	expect(
		asked.title === `Remove \u201C${name}\u201D?` &&
			JSON.stringify(asked.buttons) === JSON.stringify(["Remove", "Cancel"]) &&
			!(await app.state()).extensionsMenu,
		`the remove popup: ${JSON.stringify(asked)}`,
	);

	await app.render("remove-popup.png");
	await app.click("pane.extensionPopup.reportAbuse");

	await app.waitFor(
		"Report abuse ticked",
		(state) => state.sheet?.reportAbuse === true,
	);

	const tabsBefore = (await app.state()).profiles.find(
		(each) => each.id === profile,
	)?.tabs.length;

	await app.click("pane.extensionPopup.cancel");
	await app.waitFor("the popup gone", (state) => state.sheet === null);
	await Bun.sleep(500);
	const kept = await app.state();

	expect(
		item(kept) !== undefined &&
			kept.profiles.find((each) => each.id === profile)?.tabs.length ===
				tabsBefore,
		"Cancel removed the probe, or opened a tab",
	);

	// Asked again, Remove takes it away.
	await remove();
	await app.click("pane.extensionPopup.accept");

	await app.waitFor(
		"the probe removed",
		(state) => item(state) === undefined,
		30_000,
	);
};

export default extensionsMenu;
