import { writeFileSync } from "node:fs";
import { join } from "node:path";
import { openPage, type Page } from "../lib/cdp";
import { DARK_READER, installById } from "../lib/extensions";
import { poll } from "../lib/poll";
import { waitForTarget } from "../lib/targets";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

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

	// The item's button once it reads `wanted`; on a timeout, the buttons
	// last seen, so the store's own wording shows in the failure.
	const installLabel = async (wanted: RegExp, description: string) => {
		let seen: Button[] = [];

		try {
			return await poll(
				description,
				async () => {
					seen = await buttons();
					const found = seen.find((button) => button.install);

					return found && wanted.test(found.text) ? found.text : null;
				},
				30_000,
				100,
			);
		} catch (error) {
			throw new Error(
				`${(error as Error).message}; the buttons: ${JSON.stringify(seen.slice(0, 12))}`,
			);
		}
	};

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

	await installLabel(/^Add to Pane$/, "Add to Pane");
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

	await screenshot(page, "web-store-add-to-pane.png");

	// The script starts as the document commits, before its first paint.
	const injections = () =>
		trace().filter((entry) => entry.name === "WebStore.renameButtons");

	await poll("the store script's report", () => injections()[0]);

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
	// the same document, which the rename follows. The store loads some of
	// its links as new documents (it did for the first one on 2026-11-11):
	// each of the first three item links is tried until one moves in place.
	let movedInPlace = false;

	for (let index = 0; index < 3 && !movedInPlace; index++) {
		await page.navigate(`${item}?hl=en`);
		await installLabel(/^Add to Pane$/, "Add to Pane on Dark Reader's page");
		await page.evaluate("window.paneSameDocument = true");

		const nextItem = await page.evaluate<string | null>(
			`(() => {
				const links = [...document.querySelectorAll('a[href*="/detail/"]')]
					.filter((a) => !a.href.includes(${JSON.stringify(DARK_READER)}));
				const link = links[${index}];
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

		await installLabel(/^Add to Pane$/, "the other item's install button");

		movedInPlace =
			(await page.evaluate<boolean | null>(
				"window.paneSameDocument ?? null",
			)) === true;
	}

	if (!movedInPlace) {
		context.defer(
			"the rename following the store's in-document move: none of its first three item links moved in place this run",
		);
	}

	// Installing redraws the button as the remove button.
	await page.navigate(`${item}?hl=en`);
	await installLabel(/^Add to Pane$/, "Add to Pane before installing");
	await installById(app, profile, DARK_READER);

	// Dark Reader opens its welcome tab; the store's tab is the one to show.
	await app.send("select", { tab });

	try {
		await installLabel(/^Remove from Pane$/, "Remove from Pane");
	} catch {
		// The store learns of installs it did not start when it loads.
		await page.navigate(`${item}?hl=en`);
		await installLabel(/^Remove from Pane$/, "Remove from Pane");
	}

	await screenshot(page, "web-store-remove-from-pane.png");

	// In French, whatever verb the store uses for it: Pane where it says
	// Chrome. The store learns of the install as its page loads, and not on
	// every load: on 2026-11-11 it showed its install button in French, two
	// loads running, right after its remove button in English. Then the
	// remove label can't be read, and the install label shows the rename.
	const frenchRemove = async () => {
		await page.navigate(`${item}?hl=fr`);
		await installLabel(/^(?!.*Chrome).* de Pane$/, "the fr remove button");
	};

	await frenchRemove()
		.catch(frenchRemove)
		.catch(async (error: Error) => {
			const shown = (await buttons()).find((button) => button.install);

			if (shown?.text !== "Ajouter à Pane") {
				throw error;
			}

			context.defer(
				"the fr remove label: the store showed its install button in French (Ajouter à Pane, renamed) for the item it had just shown installed in English",
			);
		});

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
};

export default webStoreLabels;
