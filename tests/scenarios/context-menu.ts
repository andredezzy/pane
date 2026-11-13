import { isScreenLocked } from "../lib/cua";
import { launchPane, type PaneApp } from "../lib/pane";
import {
	clearClipboard,
	clipboardText,
	saveClipboard,
} from "../lib/pasteboard";
import { poll, pollValue } from "../lib/poll";
import { WindowWatcher } from "../lib/surfaces";
import { clickAt } from "../lib/traced";
import { expect, type Scenario, type ScenarioContext } from "../run";

type Point = { x: number; y: number };

type Send = (
	method: string,
	params?: Record<string, unknown>,
) => Promise<unknown>;

const PLAIN_MENU =
	"Back|Forward|Reload|Save As…|Print…|View Page Source|Inspect";

// What Chrome adds to every menu while a screen reader runs.
const IMAGE_DESCRIPTIONS = "Get Image Descriptions from Google";

// The next menu `app`'s page shows, not the last one still held: each state
// is a new object, so only the count tells them apart. It shows one of the
// `expected` titles.
function menusOf(app: PaneApp, send: Send) {
	return async (point: Point, ...expected: string[]) => {
		const before = (await app.state()).contextMenusShown;
		await clickAt(send, point.x, point.y, "right");

		const state = await app.waitFor(
			`a menu with ${expected.join(" or ")}`,
			(current) =>
				current.contextMenusShown > before &&
				(current.contextMenu ?? []).some((item) =>
					expected.includes(item.title),
				),
		);

		return (state.contextMenu ?? []).map((item) => item.title);
	};
}

// Right-clicks build Chrome's own menu (U2), with what Pane can't support
// taken off (Paper 10.4 and 10.4.1): a link's items, spelling suggestions in
// a text field, navigation, Save As…, Print…, View Page Source, and Inspect
// on plain text, and never a link opened in a new window, in Split View, in
// an incognito window, or as another of Chrome's profiles. Each chosen item
// does its job: opening the link, Copy Link Address, the field's Select All,
// Copy, Cut, and Paste, a suggestion, and Inspect. The copies go to the Mac's
// clipboard, which is put back after. The menu the system draws is the
// front's: in the background, the channel chooses from Chrome's menu model as
// a click does. With a screen reader on, the menus are the same: Google's
// image descriptions, which Chrome adds to each then, show in none.
const contextMenu: Scenario = async (context) => {
	const app = await launchPane(context);
	const watcher = new WindowWatcher(app).start();
	const profile = await app.createProfile("Menus");
	const url = context.fixtures.url("context.html");
	const { tab, page } = await app.openPage(profile, url);

	const send = (method: string, params?: Record<string, unknown>) =>
		page.send(method, params);

	const menuAt = menusOf(app, send);

	// Runs the item titled `title`, as a click in the menu; in front, the
	// system's menu then goes before the next right-click, as a user's does.
	const run = async (title: string) => {
		await app.send("chooseContextMenuItem", { title });

		if (context.foreground) {
			await app.waitFor("the menu gone", (state) => state.contextMenu === null);
		}
	};

	// Chooses an item the menu shows enabled.
	const choose = async (title: string) => {
		const item = (await app.state()).contextMenu?.find(
			(each) => each.title === title,
		);

		expect(item?.enabled, `${title} is not enabled`);
		await run(title);
	};

	// A link: open it in a new tab beside this one.
	const linkMenu = await menuAt(
		await page.evaluate<Point>("centerOf('link')"),
		"Open Link in New Tab",
	);

	expect(
		linkMenu.slice(0, 3).join("|") ===
			"Open Link in New Tab|Save Link As…|Copy Link Address",
		`link menu ${linkMenu}`,
	);

	// What Pane can't support shows nowhere.
	const unsupported = linkMenu.filter(
		(title) =>
			title === "Open Link in New Window" ||
			title === "Open Link in Split View" ||
			title === "Open Link in Incognito Window" ||
			title.startsWith("Open Link as"),
	);

	expect(unsupported.length === 0, `link menu offers ${unsupported}`);

	await run("Open Link in New Tab");

	const opened = await app.waitFor(
		"the link's tab",
		(state) => (state.profiles[0]?.tabs.length ?? 0) === 2,
	);

	const newTab = opened.profiles[0]?.tabs[1];

	expect(
		newTab?.url.includes("nav-b.html?from=context"),
		`new tab at ${newTab?.url}`,
	);

	expect(opened.activeTabID === tab, "the new tab took the selection");

	// A misspelled word: the spellchecker's suggestions replace it. The word
	// must be one macOS's spellchecker rejects ("helo", "teh", "speling" pass
	// it). On a locked screen either menu is accepted: macOS's spellchecker
	// has flagged "recieve" there (L1's regression, 2026-11-11), and earlier
	// runs read it as not flagging words there, so the plain edit menu, with
	// Select All, which Chromium leaves out on a flagged word, is accepted too.
	await page.evaluate(
		"(() => { const field = document.getElementById('text'); field.focus(); const range = document.createRange(); range.selectNodeContents(field); range.collapse(false); const selection = getSelection(); selection.removeAllRanges(); selection.addRange(range); })()",
	);

	await page.send("Input.insertText", { text: " " });
	await Bun.sleep(2_000);

	const locked = await isScreenLocked();

	const editMenu = await menuAt(
		await page.evaluate<Point>("misspelledPoint()"),
		...(locked ? ["Select All", "Add to Dictionary"] : ["Add to Dictionary"]),
	);

	if (editMenu.includes("Add to Dictionary")) {
		const suggestion = editMenu[0];

		expect(
			suggestion &&
				suggestion !== "No Guesses Found" &&
				suggestion !== "Add to Dictionary",
			`spelling menu ${editMenu}`,
		);

		await run(suggestion);

		const text = await pollValue(
			() =>
				page.evaluate<string>(
					"document.getElementById('text').innerText.trim()",
				),
			(current) => current !== "Pane recieve",
			5_000,
		);

		expect(
			text === `Pane ${suggestion}`,
			`the text became ${JSON.stringify(text)}`,
		);
	} else {
		expect(
			locked,
			`no spelling suggestions on an unlocked screen: ${editMenu}`,
		);

		expect(
			["Undo", "Redo", "Cut", "Copy", "Paste", "Select All", "Inspect"].every(
				(title) => editMenu.includes(title),
			),
			`edit menu ${editMenu}`,
		);

		context.defer(
			"spelling suggestions: no word is flagged on a locked screen",
		);
	}

	// The field's edit commands and the link's address, through the menu.
	const restoreClipboard = await saveClipboard(
		context.tempDir("pane-clipboard-"),
	);

	try {
		const field = () =>
			page.evaluate<string>("document.getElementById('text').innerText.trim()");

		const selected = () =>
			page.evaluate<string>("getSelection().toString().trim()");

		const copied = (text: string, what: string) =>
			poll(
				what,
				async () => (await clipboardText()).trim() === text,
				5_000,
			).catch(async () => {
				throw new Error(
					`${what}: the clipboard holds ${JSON.stringify(await clipboardText())}, not ${JSON.stringify(text)}`,
				);
			});

		const text = await field();
		const word = await page.evaluate<Point>("misspelledPoint()");
		const box = await page.evaluate<Point>("centerOf('text')");

		await menuAt(word, "Select All");
		await choose("Select All");
		await poll("the field selected", async () => (await selected()) === text);

		await menuAt(word, "Copy");
		await choose("Copy");
		await copied(text, "the field's text copied");

		await clearClipboard();
		await menuAt(word, "Cut");
		await choose("Cut");
		await poll("the field emptied", async () => (await field()) === "");
		await copied(text, "the field's text cut");

		await menuAt(box, "Paste");
		await choose("Paste");
		await poll("the field pasted into", async () => (await field()) === text);

		const link = await page.evaluate<string>(
			"document.getElementById('link').href",
		);

		await menuAt(
			await page.evaluate<Point>("centerOf('link')"),
			"Copy Link Address",
		);

		await choose("Copy Link Address");
		await copied(link, "the link's address copied");

		// Chrome's menu copies with no "Link copied" toast (patch 0003's toast
		// hunks): Pane's window shows no toasts of Chrome's.
		const toasts = await app.browser.cdp
			.send<{ histogram: { count: number } }>("Browser.getHistogram", {
				name: "Toast.TriggeredToShow",
			})
			.then(
				(reply) => reply.histogram.count,
				() => 0,
			);

		expect(toasts === 0, `Chrome showed ${toasts} toasts`);
	} finally {
		await restoreClipboard();
	}

	// Plain text: navigation, then Inspect opens DevTools on the element.
	await page.evaluate("document.activeElement.blur()");

	const plainMenu = await menuAt(
		await page.evaluate<Point>("centerOf('plain')"),
		"Inspect",
	);

	expect(plainMenu.join("|") === PLAIN_MENU, `plain menu ${plainMenu}`);

	await run("Inspect");
	await app.waitFor("DevTools", (state) => state.devToolsTabs.includes(tab));
	await app.send("toggleDevTools");

	await app.waitFor(
		"DevTools to close",
		(state) => !state.devToolsTabs.includes(tab),
	);

	// A menu that closed is let go, with the page its items hold: none of
	// them runs any more.
	await app.waitFor(
		"the closed menu released",
		(state) => state.contextMenu === null,
		5_000,
	);

	const stale = await app
		.send("chooseContextMenuItem", { title: "Reload" })
		.then(
			() => "ran",
			(error: unknown) => String(error),
		);

	expect(
		stale.includes("no context menu item"),
		`a closed menu's Reload: ${stale}`,
	);

	await watcher.stop();
	await app.browser.close();
	await screenReaderMenus(context, url);
};

// A copy with Chrome's screen reader mode on from its launch, whatever the
// Mac's own VoiceOver is: `--force-renderer-accessibility` with no value is
// Chrome's screen reader bundle (kAXModeComplete | kScreenReader), which
// makes Chrome take a screen reader to be running, as VoiceOver does. The
// link's and the plain text's menus, with no image descriptions.
async function screenReaderMenus(context: ScenarioContext, url: string) {
	const app = await launchPane(context, {
		args: ["--force-renderer-accessibility"],
	});

	const profile = await app.createProfile("Reader");
	const { page } = await app.openPage(profile, url);
	const menuAt = menusOf(app, (method, params) => page.send(method, params));

	// The switch reached Chrome: its accessibility was on from the launch
	// (BrowserAccessibilityStateImpl records it once, true only so).
	const forced = await app.browser.cdp
		.send<{ histogram: { sum: number } }>("Browser.getHistogram", {
			name: "Accessibility.ManuallyEnabled",
		})
		.then(
			(reply) => reply.histogram.sum,
			() => 0,
		);

	expect(forced > 0, "Chrome's accessibility was not on from the launch");

	const linkMenu = await menuAt(
		await page.evaluate<Point>("centerOf('link')"),
		"Open Link in New Tab",
	);

	expect(
		!linkMenu.includes(IMAGE_DESCRIPTIONS),
		`link menu with a screen reader ${linkMenu}`,
	);

	const plainMenu = await menuAt(
		await page.evaluate<Point>("centerOf('plain')"),
		"Inspect",
	);

	expect(
		plainMenu.join("|") === PLAIN_MENU,
		`plain menu with a screen reader ${plainMenu}`,
	);
}

export default contextMenu;
