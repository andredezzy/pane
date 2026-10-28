import { isScreenLocked } from "../lib/cua";
import { tabOf } from "../lib/pane";
import {
	clearClipboard,
	clipboardText,
	saveClipboard,
} from "../lib/pasteboard";
import { poll } from "../lib/poll";
import { WindowWatcher } from "../lib/surfaces";
import { clickAt, launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

type Point = { x: number; y: number };

// Right-clicks build the app's menu, in the Electron app's order: a link's
// items, spelling suggestions in a text field, navigation and Inspect on
// plain text. Each chosen item does its job: opening the link, Copy Link
// Address, the field's Select All, Copy, Cut, and Paste, a suggestion, and
// Inspect. The copies go to the Mac's clipboard, which is put back after.
const contextMenu: Scenario = async (context) => {
	const { app, has } = await launchTraced(context, "context-menu");
	const watcher = new WindowWatcher(app).start();
	const profile = await app.createProfile("Menus");
	const url = context.fixtures.url("context.html");
	const tab = await app.openTab(profile, url);
	const page = await app.page(tab, url);

	const send = (method: string, params?: Record<string, unknown>) =>
		page.send(method, params);

	const menuAt = async (point: Point, expected: string) => {
		const before = (await app.state()).contextMenu;
		await clickAt(send, point.x, point.y, "right");

		const state = await app.waitFor(
			`a menu with ${expected}`,
			(current) =>
				current.contextMenu !== before &&
				(current.contextMenu ?? []).some((item) => item.title === expected),
		);

		return (state.contextMenu ?? []).map((item) => item.title);
	};

	// Chooses an item the menu shows enabled.
	const choose = async (title: string) => {
		const item = (await app.state()).contextMenu?.find(
			(each) => each.title === title,
		);

		expect(item?.enabled, `${title} is not enabled`);
		await app.send("chooseContextMenuItem", { title });
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

	await app.send("chooseContextMenuItem", { title: "Open Link in New Tab" });

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
	// it). On a locked screen a plain edit menu is accepted: spelling there
	// was only ever tried with "helo", so it is unproven either way.
	let spelling: Record<string, unknown>;

	await page.evaluate(
		"(() => { const field = document.getElementById('text'); field.focus(); const range = document.createRange(); range.selectNodeContents(field); range.collapse(false); const selection = getSelection(); selection.removeAllRanges(); selection.addRange(range); })()",
	);

	await page.send("Input.insertText", { text: " " });
	await Bun.sleep(2_000);

	const locked = await isScreenLocked();

	const editMenu = await menuAt(
		await page.evaluate<Point>("misspelledPoint()"),
		locked ? "Select All" : "Add to Dictionary",
	);

	if (editMenu.includes("Add to Dictionary")) {
		const suggestion = editMenu[0];

		expect(
			suggestion &&
				suggestion !== "No Guesses Found" &&
				suggestion !== "Add to Dictionary",
			`spelling menu ${editMenu}`,
		);

		await app.send("chooseContextMenuItem", { title: suggestion });

		let text = "";
		const until = Date.now() + 5_000;

		while (Date.now() < until) {
			text = await page.evaluate<string>(
				"document.getElementById('text').innerText.trim()",
			);

			if (text !== "Pane recieve") {
				break;
			}

			await Bun.sleep(100);
		}

		expect(
			text === `Pane ${suggestion}`,
			`the text became ${JSON.stringify(text)}`,
		);

		expect(
			has("PaneContextMenu.replaceMisspelling"),
			"trace lacks the replacement",
		);

		spelling = { menu: editMenu, replaced: text };
	} else {
		expect(
			locked,
			`no spelling suggestions on an unlocked screen: ${editMenu}`,
		);

		expect(
			editMenu.join("|") ===
				"Emoji & Symbols|Undo|Redo|Cut|Copy|Paste|Delete|Select All|Inspect",
			`edit menu ${editMenu}`,
		);

		context.defer(
			"spelling suggestions: no word is flagged on a locked screen",
		);

		spelling = { menu: editMenu };
	}

	// The field's edit commands and the link's address, through the menu.
	const restoreClipboard = await saveClipboard(
		context.tempDir("pane-clipboard-"),
	);

	let edits: Record<string, unknown>;

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

		for (const name of [
			"selectAll",
			"copy",
			"cut",
			"paste",
			"copyLinkAddress",
		]) {
			expect(has(`PaneContextMenu.${name}`), `trace lacks ${name}`);
		}

		// Pane's menu writes the clipboard itself, never through Chrome's
		// menu and its "Link copied" toast (patch 0003's toast hunks).
		const toasts = await app.browser.cdp
			.send<{ histogram: { count: number } }>("Browser.getHistogram", {
				name: "Toast.TriggeredToShow",
			})
			.then(
				(reply) => reply.histogram.count,
				() => 0,
			);

		edits = { text, link, toasts };
	} finally {
		await restoreClipboard();
	}

	// Plain text: navigation, then Inspect opens DevTools on the element.
	await page.evaluate("document.activeElement.blur()");

	const plainMenu = await menuAt(
		await page.evaluate<Point>("centerOf('plain')"),
		"Inspect",
	);

	expect(
		plainMenu.join("|") === "Back|Forward|Reload|Inspect",
		`plain menu ${plainMenu}`,
	);

	await app.send("chooseContextMenuItem", { title: "Inspect" });
	await app.waitFor("DevTools", (state) => state.devToolsTabs.includes(tab));
	await app.send("toggleDevTools");

	const closed = await app.waitFor(
		"DevTools to close",
		(state) => !state.devToolsTabs.includes(tab),
	);

	expect(
		has("PaneWebContentsDelegate.showContextMenu"),
		"trace lacks showContextMenu",
	);

	expect(has("PaneContextMenu.inspectElement"), "trace lacks Inspect");

	const windows = await watcher.stop();

	expect(
		windows.others.length === 0,
		`Chrome windows appeared: ${JSON.stringify(windows.others)}`,
	);

	return {
		menus: { link: linkMenu, plain: plainMenu },
		spelling,
		edits,
		newTab,
		devToolsClosed: !closed.devToolsTabs.includes(tab),
		tab: tabOf(closed, tab),
		windows,
	};
};

export default contextMenu;
