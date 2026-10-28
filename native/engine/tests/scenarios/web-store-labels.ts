import { writeFileSync } from "node:fs";
import { join } from "node:path";
import { openPage, type Page } from "../lib/cdp";
import { poll } from "../lib/poll";
import { answerSheet, waitForSheet } from "../lib/surfaces";
import { waitForTarget } from "../lib/targets";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

const DARK_READER = "eimadpbcbfnmbkopoojfekhnkhdbieeh";
const STORE = "https://chromewebstore.google.com";

type Button = { text: string; install: boolean };

// The live Chrome Web Store: its install and remove buttons say Pane ("Add
// to Pane", "Remove from Pane"), in the store's languages too, as the store
// draws them, redraws them on install, and draws new ones when it moves to
// another item without loading a page. Every other button and text keeps
// saying Chrome, and a look-alike button on another site is left alone.
const webStoreLabels: Scenario = async (context) => {
	const { app, trace } = await launchTraced(context, "web-store-labels");
	const profile = await app.createProfile("Store", "BLUE");
	const item = `${STORE}/detail/dark-reader/${DARK_READER}`;
	const tab = await app.openTab(profile, `${item}?hl=en`);

	const target = await waitForTarget(
		app.browser.cdp,
		"the store page",
		(info) =>
			info.type === "page" &&
			info.url.startsWith(STORE) &&
			info.title.includes("Dark Reader"),
		60_000,
	);

	const page = await openPage(app.browser.cdp, "about:blank", target.targetId);

	// Every button with text, and whether it is the item's install or remove
	// button (by where the store puts it, not by its label).
	const buttons = () =>
		page.evaluate<Button[]>(
			`[...document.querySelectorAll("button")]
				.filter((button) => button.textContent.trim())
				.map((button) => ({
					text: button.textContent.trim().replace(/\\s+/g, " "),
					install: !!button.closest('[jsaction*="(wQO0od)"], [jsaction*="(ajZLRd)"]'),
				}))`,
		);

	const installLabel = (wanted: RegExp, description: string) =>
		poll(
			description,
			async () => {
				const found = (await buttons()).find((button) => button.install);

				return found && wanted.test(found.text) ? found.text : null;
			},
			30_000,
			100,
		);

	// The install button and what is around it: the store's own cards (it
	// asks browsers it does not take for Chrome to switch) can cover the
	// button in a capture of the whole page.
	const screenshot = async (shot: Page, name: string) => {
		const box = await shot.evaluate<{
			x: number;
			y: number;
			width: number;
			height: number;
		}>(
			`(() => {
				const button = document.querySelector('[jsaction*="(wQO0od)"] button, [jsaction*="(ajZLRd)"] button');
				button.scrollIntoView({ block: "center" });
				const { x, y, width, height } = button.getBoundingClientRect();
				return { x: x + scrollX, y: y + scrollY, width, height };
			})()`,
		);

		const { data } = await shot.send<{ data: string }>(
			"Page.captureScreenshot",
			{
				format: "png",
				clip: {
					x: Math.max(0, box.x - 640),
					y: Math.max(0, box.y - 120),
					width: box.width + 680,
					height: box.height + 240,
					scale: 2,
				},
			},
		);

		writeFileSync(join(context.out, name), Buffer.from(data, "base64"));

		return name;
	};

	const added = await installLabel(/^Add to Pane$/, "Add to Pane");
	const english = await buttons();

	const renamedElsewhere = english.filter(
		(button) => !button.install && /Pane/.test(button.text),
	);

	expect(
		renamedElsewhere.length === 0,
		`buttons other than the install button renamed: ${JSON.stringify(renamedElsewhere)}`,
	);

	expect(
		(await page.evaluate<string>("document.title")).includes(
			"Chrome Web Store",
		),
		"the page's own title changed",
	);

	const addShot = await screenshot(page, "web-store-add-to-pane.png");

	// The script starts as the document commits, before its first paint.
	const injections = () =>
		trace().filter((entry) => entry.name === "WebStore.renameButtons");

	const first = await poll("the store script's report", () => injections()[0]);

	// Other languages. French says Google Chrome, which becomes Pane.
	const languages: Record<string, string> = {};

	for (const [hl, wanted] of [
		["fr", /^Ajouter à Pane$/],
		["ja", /^Pane に追加$/],
		["pt-BR", /^Usar no Pane$/],
	] as const) {
		await page.navigate(`${item}?hl=${hl}`);
		languages[hl] = await installLabel(wanted, `the ${hl} install button`);
	}

	await page.navigate(`${item}?hl=en`);
	await installLabel(/^Add to Pane$/, "Add to Pane again");

	// Another item, reached the store's own way: it draws the new page in
	// the same document.
	await page.evaluate("window.paneSameDocument = true");

	const nextItem = await page.evaluate<string | null>(
		`(() => {
			const link = [...document.querySelectorAll('a[href*="/detail/"]')]
				.find((a) => !a.href.includes(${JSON.stringify(DARK_READER)}));
			if (!link) return null;
			link.click();
			return link.href;
		})()`,
	);

	expect(nextItem !== null, "the store page links to no other item");

	await poll(
		"the other item's page",
		async () =>
			!(await page.evaluate<string>("location.href")).includes(DARK_READER),
		30_000,
	);

	const otherItem = await installLabel(
		/^Add to Pane$/,
		"the other item's install button",
	);

	expect(
		(await page.evaluate<boolean | null>("window.paneSameDocument ?? null")) ===
			true,
		"the store loaded a new document for the other item",
	);

	// Installing redraws the button as the remove button.
	await page.navigate(`${item}?hl=en`);
	await installLabel(/^Add to Pane$/, "Add to Pane before installing");
	await app.send("installExtension", { id: DARK_READER, profile });
	const sheet = await waitForSheet(app, "extensionInstall", 60_000);
	await answerSheet(app, sheet.buttons?.[0] ?? "Add extension");

	await app.waitFor(
		"Dark Reader installed",
		(state) =>
			(state.extensions[profile] ?? []).some(
				(installed) => installed.id === DARK_READER && installed.enabled,
			),
		120_000,
	);

	// Dark Reader opens its welcome tab; the store's tab is the one to show.
	await app.send("select", { tab });

	let removed: string;

	try {
		removed = await installLabel(/^Remove from Pane$/, "Remove from Pane");
	} catch {
		// The store learns of installs it did not start when it loads.
		await page.navigate(`${item}?hl=en`);
		removed = await installLabel(/^Remove from Pane$/, "Remove from Pane");
	}

	const removeShot = await screenshot(page, "web-store-remove-from-pane.png");

	const removedFr = await (async () => {
		await page.navigate(`${item}?hl=fr`);

		return installLabel(/^Supprimer de Pane$/, "the fr remove button");
	})();

	// Another site with the store's markup: left alone.
	const lookalikeURL = context.fixtures.url("web-store-lookalike.html");
	await page.navigate(lookalikeURL);

	const lookalike = await page.evaluate<string>(
		"document.querySelector('button').textContent.trim()",
	);

	expect(
		lookalike === "Add to Chrome",
		`a button on another site was renamed: ${lookalike}`,
	);

	return {
		added,
		otherButtons: english
			.filter((button) => !button.install)
			.map((button) => button.text),
		firstInjection: first.args,
		injections: injections().length,
		languages,
		otherItem: { url: nextItem, label: otherItem },
		removed,
		removedFr,
		lookalike,
		screenshots: [addShot, removeShot],
	};
};

export default webStoreLabels;
