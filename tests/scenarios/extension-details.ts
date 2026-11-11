import { join } from "node:path";
import {
	DETAILS,
	detailsShown,
	detailsURL,
	EXTENSIONS_PAGE,
	extensionsPage,
	ITEMS,
	press,
	selectedAt,
	typeText,
} from "../lib/chrome-extensions";
import { extensionId, workerPage } from "../lib/extensions";
import { launchPane } from "../lib/pane";
import { poll } from "../lib/poll";
import { expect, type Scenario } from "../run";

const PROBE = join(import.meta.dir, "..", "pages", "extensions", "details");
const NAME = "Pane details probe";

// The site access pop-up of the probe's Details.
const HOST_ACCESS = `${DETAILS}.querySelector("extensions-runtime-host-permissions").shadowRoot.querySelector("#hostAccess, #newHostAccess")`;
// The page's own Add a site dialog, and its field.
const HOSTS = `${DETAILS}.querySelector("extensions-runtime-host-permissions").shadowRoot`;
const ADD_SITE = `${HOSTS}.querySelector("extensions-runtime-hosts-dialog")?.shadowRoot`;

// An extension's Details are Chrome's own (chrome://extensions/?id=, D3,
// Paper 10.2) in the profile's tab. Manage Extension, from the extensions
// menu's more menu, opens them there: the probe (every site, downloads,
// notifications, an options page, a homepage) reads its size, source, and
// permissions in Chrome's words, Site access On all sites, Allow access to
// file URLs, and no Allow in Incognito, which Pane has none of. Each Site
// access choice changes the sites the probe has (chrome.permissions.getAll):
// On click takes every site away; On specific sites, with none yet, asks
// for one in the page's own dialog, which refuses what is no site and adds
// one that is; that site's own menu removes it, the list empty and the
// probe with no site;
// the action's own menu's On All Sites gives every site back, and Details
// follows it. Allow access to file URLs sets Chrome's setting both ways. The back arrow shows the list; View Web
// Permissions and the list's Details open them again. The pop-up is chosen
// with the keyboard, its options' first letter typed as a person types it:
// a click on the page's own select opens the system's menu, which runs a
// loop of its own no background run can choose in. Each other press is a
// real click on the page (CDP's Input).
const extensionDetails: Scenario = async (context) => {
	const app = await launchPane(context, {
		args: [`--load-extension=${PROBE}`],
	});

	// Light, whatever the Mac's appearance, for the captures.
	await app.send("theme", { theme: "light" });

	const profile = await app.createProfile("Details");
	const url = context.fixtures.url("light.html?extension-details");
	const { tab: fixture } = await app.openPage(profile, url);

	const id = await extensionId(app, profile, NAME);
	const worker = await workerPage(app, id, url);

	const origins = () =>
		worker.evaluate<string[]>(
			"chrome.permissions.getAll().then((all) => all.origins)",
		);

	const sitesWhen = (
		description: string,
		check: (origins: string[]) => boolean,
	) =>
		poll(
			`the probe's sites ${description}`,
			async () => {
				const current = await origins();

				return check(current) && current;
			},
			10_000,
		);

	// Manage Extension, from the extensions menu's more menu.
	const manageExtension = async () => {
		await app.send("extensionsMenu", { control: "puzzle" });
		await app.waitFor("the extensions menu", (state) => state.extensionsMenu);
		await app.send("extensionsMenu", { control: "more", id });

		await app.waitFor(
			"the more menu",
			(state) => state.extensionMoreMenu === id,
		);

		await app.send("extensionsMenu", { control: "manageExtension" });
	};

	await manageExtension();
	const { tab, page } = await extensionsPage(app, profile, detailsURL(id));

	// Details once `check` holds; the last it showed, when it never does.
	const details = async (
		description: string,
		check: (shown: Awaited<ReturnType<typeof detailsShown>>) => boolean,
	) => {
		let last: unknown = null;

		return poll(
			`Details ${description}`,
			async () => {
				const shown = await detailsShown(page).catch(() => null);
				last = shown;

				return shown && check(shown) ? shown : null;
			},
			10_000,
		).catch((error: Error) => {
			throw new Error(
				`${error.message}; Details showed ${JSON.stringify(last)}`,
			);
		});
	};

	const shown = await details(
		"of the probe, its size measured",
		(current) => current.name === NAME && current.size !== "",
	);

	expect(
		shown.size === "< 1 MB" &&
			shown.source.includes("Unpacked extension") &&
			shown.permissions.includes("Display notifications") &&
			shown.permissions.includes("Manage your downloads"),
		`Details reads ${JSON.stringify(shown)}`,
	);

	expect(
		shown.siteAccess === "On all sites" &&
			shown.fileAccessRow &&
			shown.homepage &&
			!shown.webStore &&
			shown.options,
		`Details reads ${JSON.stringify(shown)}`,
	);

	// Pane has no incognito: its row is hidden (Paper 10.2, the user's
	// answer).
	expect(!shown.incognitoRow, "Details shows Allow in Incognito");

	// The tab is the profile's, titled as Chrome titles it.
	const title = await page.evaluate<string>("document.title");

	expect(
		title === `Extensions - ${NAME}`,
		`the tab reads ${JSON.stringify(title)}`,
	);

	// The pop-up, its next choice by its options' shared first letter, as a
	// select cycles through them: On click, On specific sites, On all sites.
	const nextChoice = async (from: string) => {
		await page.evaluate(`${HOST_ACCESS}.focus()`);
		// A new search: the last letter typed is more than a second old.
		await Bun.sleep(1_100);
		await typeText(page, "o");

		return poll(
			`the pop-up past ${from}`,
			async () => {
				const current = (await detailsShown(page)).siteAccess;

				return current !== from && current;
			},
			5_000,
		).catch(() => from);
	};

	// On click: every site goes.
	const onClick = await nextChoice("On all sites");

	expect(onClick === "On click", `the pop-up went to ${onClick}`);

	await sitesWhen(
		"without every site",
		(current) => !current.includes("<all_urls>"),
	);

	// On specific sites, with none yet, asks for one in the page's dialog.
	await nextChoice("On click");

	await poll(
		"the page's Add a site",
		() => page.evaluate<boolean>(`!!${ADD_SITE}?.querySelector("#input")`),
		5_000,
	);

	const field = `${ADD_SITE}.querySelector("#input")`;
	const addButton = `${ADD_SITE}.querySelector("#submit")`;

	await press(page, field, "the dialog's field");
	await typeText(page, "not a site");
	await press(page, addButton, "Add");

	const refused = await poll(
		"Add a site refusing what is no site",
		() =>
			page.evaluate<boolean>(`!!${ADD_SITE}?.querySelector("#input")?.invalid`),
		5_000,
	);

	expect(refused, "Add a site took what is no site");

	// The field's text selected and replaced, as a person retypes it.
	await page.evaluate(`${field}.select()`);
	await typeText(page, "example.com");
	await press(page, addButton, "Add");

	const withSite = await details(
		"On specific sites, with example.com",
		(current) =>
			current.siteAccess === "On specific sites" &&
			current.sites.some((site) => site.includes("example.com")),
	);

	await sitesWhen(
		"with example.com alone",
		(current) =>
			!current.includes("<all_urls>") &&
			current.some((origin) => origin.includes("example.com")),
	);

	// The site's own menu removes it; with none left, the probe is on click.
	// Its ⋮, or its ×, whichever the page shows (the enhanced site controls
	// show the ×).
	await press(
		page,
		`[...${HOSTS}.querySelectorAll("#hosts li .open-edit-host, #hosts li .remove-host")].find((button) => button.getClientRects().length > 0)`,
		"the site's menu",
	);

	const menuRemove = `${HOSTS}.querySelector("#action-menu-remove")`;

	if (
		await page.evaluate<boolean>(`!!${menuRemove}?.getClientRects().length`)
	) {
		await press(page, menuRemove, "Remove, in the site's menu");
	}

	await sitesWhen(
		"with no site",
		(current) => !current.some((origin) => origin.includes("example.com")),
	);

	// Chrome's page keeps On specific sites, its list empty, where Pane's own
	// Details went On click.
	await details("with no site left", (current) => current.sites.length === 0);

	// On All Sites from the action's own menu, on the page: Details follows
	// a change made elsewhere.
	await app.send("select", { tab: fixture });
	await app.send("extensionActionMenu", { id, run: "On All Sites" });

	await sitesWhen("with every site again", (current) =>
		current.includes("<all_urls>"),
	);

	await app.send("select", { tab });

	await details(
		"On all sites again",
		(current) => current.siteAccess === "On all sites",
	);

	// The back arrow: the list.
	await press(
		page,
		`${DETAILS}.querySelector("#closeButton")`,
		"the back arrow",
	);

	await poll(
		"the list",
		() =>
			page.evaluate<boolean>(
				`!!${ITEMS}?.querySelector("extensions-item")?.getClientRects().length`,
			),
		5_000,
	);

	// The list's Details: the probe's again.
	await press(
		page,
		`${ITEMS}.querySelector("extensions-item").shadowRoot.querySelector("#detailsButton")`,
		"the card's Details",
	);

	await details("again, from the card", (current) => current.name === NAME);

	// View Web Permissions, in a pinned action's right-click menu, opens them
	// in the same tab.
	await app.send("select", { tab: fixture });

	await app.send("setExtensionPinned", { id, profile, pinned: true });

	await app.waitFor("the probe pinned", (state) =>
		(state.extensions[profile] ?? []).some(
			(each) => each.id === id && each.pinned,
		),
	);

	await app.send("extensionsMenu", { control: "viewWebPermissions", id });
	const again = await selectedAt(app, profile, detailsURL(id));

	expect(
		again.tab === tab,
		`View Web Permissions opened ${again.tab}, not the page's tab ${tab}`,
	);

	// One tab of Chrome's page in the profile, whatever opened it.
	const pages = (
		again.state.profiles.find((each) => each.id === profile)?.tabs ?? []
	).filter((each) => each.url.startsWith(EXTENSIONS_PAGE));

	expect(
		pages.length === 1,
		`the profile has ${pages.length} tabs of Chrome's page`,
	);

	// Allow access to file URLs, both ways, last: Chrome reloads the
	// extension each time, and an unpacked one, as the probe is, comes back
	// off outside Developer mode, as in Chrome.
	await selectedAt(app, profile, detailsURL(id));
	const fileAccess = `${DETAILS}.querySelector("#allow-on-file-urls")`;
	const before = (await detailsShown(page)).fileAccess;
	await press(page, fileAccess, "Allow access to file URLs");

	await details(
		"with file access changed",
		(current) => current.fileAccess !== before,
	);

	await press(page, fileAccess, "Allow access to file URLs");

	await details(
		"with file access back",
		(current) => current.fileAccess === before,
	);

	return { shown, withSite: withSite.sites };
};

export default extensionDetails;
