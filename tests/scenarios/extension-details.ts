import { join } from "node:path";
import { clickButton, isScreenLocked, mainWindowOf } from "../lib/cua";
import { workerPage } from "../lib/extensions";
import type { AppState } from "../lib/pane";
import { poll } from "../lib/poll";
import { answerSheet, waitForSheet } from "../lib/surfaces";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

const PROBE = join(import.meta.dir, "..", "pages", "extensions", "details");
const NAME = "Pane details probe";

type Details = NonNullable<AppState["extensionDetails"]>;

// An extension's Details on the Extensions page, as chrome://extensions'
// details page, with a probe that asks for every site, downloads, and
// notifications, and has an options page and a homepage. Manage Extension
// opens it at once: version, size, and source, the permissions in Chrome's
// words, Site access On all sites, file access, and the homepage. Each Site
// access choice changes the sites the probe has (chrome.permissions.getAll);
// On specific sites shows once the action's menu granted one site. The back
// arrow returns to the list, and View Web Permissions and a card's Details
// open it again. Allow access to file URLs sets Chrome's setting both ways.
// Remove asks first, then leaves Details for the list.
const extensionDetails: Scenario = async (context) => {
	const { app, has } = await launchTraced(context, "extension-details", {
		args: [`--load-extension=${PROBE}`],
	});

	const profile = await app.createProfile("Details");
	const url = context.fixtures.url("light.html?extension-details");
	const tab = await app.openTab(profile, url);
	await app.page(tab, url);

	const loaded = await app.waitFor("the probe extension", (state) =>
		(state.extensions[profile] ?? []).some((item) => item.name === NAME),
	);

	const id = (loaded.extensions[profile] ?? []).find(
		(item) => item.name === NAME,
	)?.id as string;

	const worker = await workerPage(app, id, url);

	const origins = () =>
		worker.evaluate<string[]>(
			"chrome.permissions.getAll().then((all) => all.origins)",
		);

	const details = (state: AppState) =>
		state.extensionDetails?.extension === id
			? state.extensionDetails
			: undefined;

	// Manage Extension, from the extensions menu's more menu.
	await app.send("extensionsMenu", { control: "manageExtension", id });

	const opened = await app.waitFor(
		"Details, its size measured",
		(state) => state.page === "extensions" && details(state)?.size != null,
	);

	const shown = details(opened) as Details;

	expect(
		shown.source === "Unpacked extension" && shown.size === "< 1 MB",
		`Details reads source ${shown.source}, size ${shown.size}`,
	);

	expect(
		JSON.stringify(shown.permissions) ===
			JSON.stringify(["Display notifications", "Manage your downloads"]),
		`Details lists ${JSON.stringify(shown.permissions)}`,
	);

	expect(
		shown.siteAccess === "onAllSites" &&
			shown.wantsFileAccess &&
			shown.homepage === "https://example.com/pane-details-probe" &&
			shown.webStore === null,
		`Details reads ${JSON.stringify(shown)}`,
	);

	await Bun.sleep(300);

	await app.send("render", {
		path: join(context.out, "extension-details-light.png"),
	});

	// Site access, as its pop-up sets it.
	const setAccess = async (
		access: Details["siteAccess"],
		check: (origins: string[]) => boolean,
	) => {
		await app.send("setExtensionSiteAccess", { id, profile, access });

		await app.waitFor(
			`Details at ${access}`,
			(state) => details(state)?.siteAccess === access,
		);

		return poll(`the probe's sites at ${access}`, async () => {
			const current = await origins();

			return check(current) && current;
		});
	};

	const onClick = await setAccess(
		"onClick",
		(current) => !current.includes("<all_urls>"),
	);

	// On <site> from the action's menu grants the page's site: Details shows
	// On specific sites.
	const site = new URL(url).hostname;

	await app.send("extensionActionMenu", { id, run: `On ${site}` });

	await app.waitFor(
		"Details at On specific sites",
		(state) => details(state)?.siteAccess === "onSpecificSites",
	);

	const specific = await poll("the page's site granted", async () => {
		const current = await origins();

		return (
			!current.includes("<all_urls>") &&
			current.some(
				(origin) => new URL(origin.replace("*", "x")).hostname === site,
			) &&
			current
		);
	});

	await Bun.sleep(300);

	await app.send("render", {
		path: join(context.out, "extension-details-specific-light.png"),
	});

	const onAllSites = await setAccess("onAllSites", (current) =>
		current.includes("<all_urls>"),
	);

	// The back arrow, then View Web Permissions, from the browser.
	await app.send("closeExtensionDetails");

	await app.waitFor(
		"the list",
		(state) => state.page === "extensions" && state.extensionDetails === null,
	);

	await Bun.sleep(300);

	await app.send("render", {
		path: join(context.out, "extension-details-list-light.png"),
	});

	await app.send("showPage", { page: "browser" });
	await app.send("extensionsMenu", { control: "viewWebPermissions", id });

	await app.waitFor(
		"Details from View Web Permissions",
		(state) => state.page === "extensions" && details(state) !== undefined,
	);

	// The card's Details, in the dark theme.
	await app.send("closeExtensionDetails");
	await app.send("showExtensionDetails", { id, profile });
	await app.send("theme", { theme: "dark" });

	await app.waitFor(
		"Details, dark",
		(state) =>
			state.appearance.toLowerCase().includes("dark") &&
			details(state)?.size != null,
	);

	await Bun.sleep(300);

	await app.send("render", {
		path: join(context.out, "extension-details-dark.png"),
	});

	await app.send("theme", { theme: "light" });

	// Allow access to file URLs, both ways, as Chrome holds it. Chrome
	// reloads the extension for it, and reloads an unpacked one such as the
	// probe as one it keeps disabled outside developer mode, so the probe's
	// own view is not read.
	const fileAccess = async (allowed: boolean) => {
		await app.send("setExtensionFileAccess", { id, profile, allowed });

		await app.waitFor(
			`Details with file access ${allowed ? "on" : "off"}`,
			(state) => details(state)?.allowsFileAccess === allowed,
		);

		return allowed;
	};

	const toggled = await fileAccess(!shown.allowsFileAccess);
	const restored = await fileAccess(shown.allowsFileAccess);

	// Remove extension asks first, in the popup; removed, the page shows the
	// list.
	let removal: unknown = "not checked: the screen is locked";

	if (await isScreenLocked()) {
		context.defer("Details' Remove, which a click on the screen presses");
	} else {
		await clickButton(
			app.browser.pid,
			await mainWindowOf(app.browser.pid),
			"Remove",
			"pane.extensionDetails.remove",
		);

		const asked = await waitForSheet(app, "extensionRemove");

		expect(
			asked.title === `Remove “${NAME}”?`,
			`the removal popup reads ${JSON.stringify(asked)}`,
		);

		const answer = await answerSheet(app, asked.buttons?.[0] ?? "Remove");

		await app.waitFor(
			"removed, the list shows",
			(state) =>
				!(state.extensions[profile] ?? []).some((item) => item.id === id) &&
				state.extensionDetails === null &&
				state.page === "extensions",
			30_000,
		);

		removal = { popup: asked, answer };
	}

	expect(
		has("PaneBrowserContext.setExtensionSiteAccess") &&
			has("PaneBrowserContext.setExtensionFileAccess"),
		"the engine set no site or file access",
	);

	return {
		extension: id,
		details: shown,
		siteAccess: { onClick, specific, onAllSites },
		fileAccess: { toggled, restored },
		removal,
	};
};

export default extensionDetails;
