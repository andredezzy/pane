import { join } from "node:path";
import { extensionId, workerPage } from "../lib/extensions";
import { tabOf } from "../lib/pane";
import { poll } from "../lib/poll";
import { targets } from "../lib/targets";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

const PROBE = join(import.meta.dir, "..", "pages", "extensions", "tabs-api");

type Discarded = { id: number; discarded: boolean; url: string } | null;

// An extension discards tabs (chrome.tabs.discard), which Chrome does by
// putting an empty page in the tab's place. The app keeps its tab, now for
// that page: a background tab loads again when selected, and the selected
// one loads again at once, its view where the old one was. Chrome's own
// discarding (memory pressure) is off (DisableTabDiscarding).
const tabDiscard: Scenario = async (context) => {
	const { app, trace } = await launchTraced(context, "tab-discard", {
		args: [`--load-extension=${PROBE}`],
	});

	const profile = await app.createProfile("Discard");
	const backURL = context.fixtures.url("nav-a.html?discard-background");
	const { tab: back, page: backPage } = await app.openPage(profile, backURL);
	await backPage.evaluate("window.loadedOnce = true");
	const frontURL = context.fixtures.url("nav-b.html?discard-front");
	await app.openPage(profile, frontURL);

	const id = await extensionId(app, profile, "Pane tabs API probe");

	const worker = await workerPage(app, id, frontURL);

	const discard = (url: string) =>
		worker.evaluate<Discarded>(
			`chrome.tabs.query({ url: ${JSON.stringify(url)} })
				.then(([tab]) => chrome.tabs.discard(tab.id))
				.then((tab) => ({ id: tab.id, discarded: tab.discarded, url: tab.url }))`,
		);

	const pageTargets = async (url: string) =>
		(await targets(app.browser.cdp)).filter(
			(target) => target.type === "page" && target.url === url,
		);

	// The background tab.
	const discardedBack = await discard(backURL);

	expect(
		discardedBack?.discarded === true,
		`the background tab was not discarded: ${JSON.stringify(discardedBack)}`,
	);

	const afterDiscard = await app.state();

	expect(
		tabOf(afterDiscard, back)?.url === backURL &&
			tabOf(afterDiscard, back)?.loaded === true,
		`the app lost the discarded tab: ${JSON.stringify(tabOf(afterDiscard, back))}`,
	);

	// Discarded, it waits in the background without loading.
	await Bun.sleep(500);

	expect(
		!trace().some((entry) => entry.name === "PaneWebContents.reloadDiscarded"),
		"the background tab loaded again before it was selected",
	);

	// Selecting it loads it again: a new document, the same tab.
	await app.send("select", { tab: back });
	const reloaded = await app.page(back, backURL);

	expect(
		(await reloaded.evaluate<boolean | null>("window.loadedOnce ?? null")) ===
			null,
		"the selected tab shows the page from before the discard",
	);

	const reloadedState = await worker.evaluate<Discarded>(
		`chrome.tabs.query({ url: ${JSON.stringify(backURL)} }).then(([tab]) => ({ id: tab.id, discarded: tab.discarded, url: tab.url }))`,
	);

	expect(
		reloadedState?.discarded === false,
		`the tab is still discarded: ${JSON.stringify(reloadedState)}`,
	);

	// The selected tab: it loads again at once, in the app's panel.
	const discardedFront = await discard(backURL);

	expect(
		discardedFront?.discarded === true,
		`the selected tab was not discarded: ${JSON.stringify(discardedFront)}`,
	);

	await poll(
		"the selected tab loaded again",
		async () => {
			const found = await pageTargets(backURL);

			return found.length === 1 ? found : null;
		},
		15_000,
	);

	await app.page(back, backURL);
	const { frame } = (await app.state()).window;

	const [width, height] = (frame.match(/\{([\d.]+), ([\d.]+)\}\}$/) ?? [])
		.slice(1)
		.map(Number);

	// The page's own view, in the middle of the panel (as window-drag reads
	// it), not the one Chrome destroyed.
	const underPanel = async () =>
		(await app.hitTest(Math.round(width * 0.6), Math.round(height / 2))).target;

	const shown = await poll(
		"the reloaded page under the panel",
		async () => {
			const target = await underPanel();

			return target === "page" ? target : null;
		},
		10_000,
	).catch(underPanel);

	expect(shown === "page", `the panel shows ${shown}, not the reloaded page`);

	const final = await app.state();

	expect(
		trace().filter((entry) => entry.name === "PaneWebContents.discarded")
			.length === 2,
		"the engine did not follow both tabs into their new pages",
	);

	expect(
		final.activeTabID === back && tabOf(final, back)?.loaded === true,
		`the app's tab changed: ${JSON.stringify(tabOf(final, back))}`,
	);
};

export default tabDiscard;
