// Chrome's extensions pages in a profile's tab (D3, Paper 10.1 to 10.3):
// chrome://extensions, an extension's Details there (?id=), and its
// shortcuts page. Found through CDP, read through their shadow roots, and
// pressed and typed into as a person would, with CDP's Input domain, which
// hands the page its events through the browser's own input path (the
// window sees a key before the page does, as a typed one).
import { openPage, type Page } from "./cdp";
import type { AppState, PaneApp } from "./pane";
import { poll } from "./poll";
import { targets } from "./targets";

export const EXTENSIONS_PAGE = "chrome://extensions/";
export const SHORTCUTS_PAGE = "chrome://extensions/shortcuts";

export function detailsURL(id: string): string {
	return `${EXTENSIONS_PAGE}?id=${id}`;
}

// The manager's shadow root, and what its views show, as expressions.
const MANAGER = `document.querySelector("extensions-manager").shadowRoot`;

export const ITEMS = `${MANAGER}.querySelector("extensions-item-list")?.shadowRoot`;
export const DETAILS = `${MANAGER}.querySelector("extensions-detail-view")?.shadowRoot`;
export const SHORTCUTS = `${MANAGER}.querySelector("extensions-keyboard-shortcuts")?.shadowRoot`;
export const SIDEBAR = `${MANAGER}.querySelector("extensions-sidebar")?.shadowRoot`;

// The selected tab of `profile`, once it shows `url` and has loaded.
export async function selectedAt(
	app: PaneApp,
	profile: string,
	url: string,
	timeoutMs = 30_000,
): Promise<{ tab: string; state: AppState }> {
	const state = await app.waitFor(
		`${url} in the selected tab of ${profile}`,
		(current) => {
			const tab = current.profiles
				.find((each) => each.id === profile)
				?.tabs.find((each) => each.id === current.activeTabID);

			return tab?.url === url && !tab.loading && current.page === "browser";
		},
		timeoutMs,
	);

	return { tab: state.activeTabID as string, state };
}

// Chrome's extensions page at `url` in the selected tab of `profile`, its
// manager drawn: the tab, and the page through CDP. `sameProfileAs` is the
// address of another page of the profile, where another profile shows the
// page too.
export async function extensionsPage(
	app: PaneApp,
	profile: string,
	url: string,
	sameProfileAs?: string,
): Promise<{ tab: string; page: Page }> {
	const { tab } = await selectedAt(app, profile, url);

	const context = sameProfileAs
		? (await targets(app.browser.cdp)).find(
				(target) => target.type === "page" && target.url === sameProfileAs,
			)?.browserContextId
		: undefined;

	const target = await poll(
		`the page target at ${url}`,
		async () =>
			(await targets(app.browser.cdp)).find(
				(each) =>
					each.type === "page" &&
					each.url === url &&
					(!context || each.browserContextId === context),
			),
		30_000,
		200,
	).catch(async (error: Error) => {
		const pages = (await targets(app.browser.cdp))
			.filter((each) => each.type === "page")
			.map((each) => each.url);

		throw new Error(`${error.message}; pages at ${JSON.stringify(pages)}`);
	});

	const page = await openPage(app.browser.cdp, "about:blank", target.targetId);

	await poll(
		`${url}'s manager drawn`,
		() =>
			page
				.evaluate<boolean>(`!!${MANAGER}?.querySelector("#viewManager")`)
				.catch(() => false),
		30_000,
		100,
	);

	return { tab, page };
}

// A press at the middle of `element` (an expression), scrolled into view
// first, as a click of the mouse's: moved there, pressed, let go.
export async function press(
	page: Page,
	element: string,
	description: string,
): Promise<void> {
	const at = await poll(
		description,
		() =>
			page
				.evaluate<{ x: number; y: number } | null>(
					`(() => {
						const element = ${element};
						if (!element) return null;
						element.scrollIntoView({ block: "center" });
						const box = element.getBoundingClientRect();
						return box.width && box.height
							? { x: box.x + box.width / 2, y: box.y + box.height / 2 }
							: null;
					})()`,
				)
				.catch(() => null),
		10_000,
		100,
	);

	for (const type of ["mouseMoved", "mousePressed", "mouseReleased"]) {
		await page.send("Input.dispatchMouseEvent", {
			type,
			x: at.x,
			y: at.y,
			button: type === "mouseMoved" ? "none" : "left",
			buttons: type === "mousePressed" ? 1 : 0,
			clickCount: type === "mouseMoved" ? 0 : 1,
		});
	}
}

// CDP's modifiers: Alt 1, Ctrl 2, Meta (⌘) 4, Shift 8.
const MODIFIER_BITS: Record<string, number> = {
	option: 1,
	control: 2,
	command: 4,
	shift: 8,
};

// The Windows virtual key code of a letter, digit, or named key.
function keyCode(key: string): number {
	const named: Record<string, number> = {
		Escape: 27,
		Enter: 13,
		Tab: 9,
		Backspace: 8,
		",": 188,
		".": 190,
		"-": 189,
		"=": 187,
	};

	return named[key] ?? key.toUpperCase().charCodeAt(0);
}

// What a key types: itself, shifted to its capital with Shift alone; none
// with ⌘, ⌃, or ⌥ held, which type no text.
function typed(key: string, modifiers: string[]): string | undefined {
	if (key.length !== 1 || modifiers.some((name) => name !== "shift")) {
		return undefined;
	}

	return modifiers.includes("shift") ? key.toUpperCase() : key;
}

// The key's code, as the DOM names a key on a US layout.
function codeOf(key: string): string {
	if (/^[a-z]$/i.test(key)) {
		return `Key${key.toUpperCase()}`;
	}

	return /^\d$/.test(key) ? `Digit${key}` : key;
}

// A key typed into the page, with `modifiers` held ("command", "shift",
// "control", "option"), down and up, through the page's input path: the
// window sees it first, as it sees a typed one.
export async function typeKey(
	page: Page,
	key: string,
	modifiers: string[] = [],
): Promise<void> {
	const bits = modifiers.reduce(
		(sum, name) => sum + (MODIFIER_BITS[name] ?? 0),
		0,
	);

	const code = keyCode(key);
	const text = typed(key, modifiers);

	const name =
		key.length === 1 && modifiers.includes("shift") ? key.toUpperCase() : key;

	await page.send("Input.dispatchKeyEvent", {
		type: text ? "keyDown" : "rawKeyDown",
		key: name,
		code: codeOf(key),
		windowsVirtualKeyCode: code,
		nativeVirtualKeyCode: code,
		modifiers: bits,
		...(text ? { text, unmodifiedText: key } : {}),
	});

	await page.send("Input.dispatchKeyEvent", {
		type: "keyUp",
		key: name,
		code: codeOf(key),
		windowsVirtualKeyCode: code,
		nativeVirtualKeyCode: code,
		modifiers: bits,
	});
}

// Text typed into what has the page's keys, a key at a time.
export async function typeText(page: Page, text: string): Promise<void> {
	for (const character of text) {
		await page.send("Input.dispatchKeyEvent", {
			type: "keyDown",
			key: character,
			text: character,
			unmodifiedText: character,
		});

		await page.send("Input.dispatchKeyEvent", {
			type: "keyUp",
			key: character,
		});
	}
}

// What the list shows of each extension: its name, and whether its switch
// is on.
export type ListedExtension = { id: string; name: string; on: boolean };

export function listed(page: Page): Promise<ListedExtension[]> {
	return page.evaluate<ListedExtension[]>(
		`[...(${ITEMS}?.querySelectorAll("extensions-item") ?? [])].map((item) => ({
			id: item.id,
			name: item.shadowRoot.querySelector("#name").textContent.trim(),
			on: item.shadowRoot.querySelector("#enableToggle").checked,
		}))`,
	);
}

// What Details shows of the extension in it, read as a person reads it.
export type DetailsShown = {
	name: string;
	on: boolean;
	size: string;
	source: string;
	permissions: string[];
	// The pop-up's choice, by the option's words; null where it shows none.
	siteAccess: string | null;
	// The sites listed under On specific sites, as the page writes them.
	sites: string[];
	pinRow: boolean;
	pinned: boolean;
	incognitoRow: boolean;
	fileAccessRow: boolean;
	fileAccess: boolean;
	homepage: boolean;
	webStore: boolean;
	options: boolean;
};

export function detailsShown(page: Page): Promise<DetailsShown> {
	return page.evaluate<DetailsShown>(
		`(() => {
			const root = ${DETAILS};
			const shown = (element) =>
				!!element && !element.hidden && element.getClientRects().length > 0;
			const text = (selector) =>
				(root.querySelector(selector)?.textContent ?? "").trim().replace(/\\s+/g, " ");
			const hosts = root.querySelector("extensions-runtime-host-permissions")?.shadowRoot;
			const select = hosts?.querySelector("#hostAccess, #newHostAccess");
			const toggle = (id) => root.querySelector(id);
			return {
				name: text("#name"),
				on: !!root.querySelector("#enableToggle")?.checked,
				size: text("#size .section-content, #size"),
				source: text("#source"),
				permissions: [...root.querySelectorAll("#permissions-list li")].map(
					(item) => item.textContent.trim().replace(/\\s+/g, " "),
				),
				siteAccess: shown(select)
					? select.options[select.selectedIndex].textContent.trim()
					: null,
				sites: [...(hosts?.querySelectorAll("#hosts li .site, #hosts .site") ?? [])].map(
					(site) => site.textContent.trim(),
				),
				pinRow: shown(toggle("#pin-to-toolbar")),
				pinned: !!toggle("#pin-to-toolbar")?.checked,
				incognitoRow: shown(toggle("#allow-incognito")),
				fileAccessRow: shown(toggle("#allow-on-file-urls")),
				fileAccess: !!toggle("#allow-on-file-urls")?.checked,
				homepage: shown(root.querySelector("#extensionWebsite")),
				webStore: shown(root.querySelector("#viewInStore")),
				options: shown(root.querySelector("#extensionsOptions")),
			};
		})()`,
	);
}
