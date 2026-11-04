import { Database } from "bun:sqlite";
import { copyFileSync, existsSync } from "node:fs";
import { join } from "node:path";
import { launchPane, type PaneApp, tabOf } from "../lib/pane";
import { poll } from "../lib/poll";
import { isRunning } from "../lib/processes";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// A profile's Save history (ADR 0119), against what the engine writes to disk.
// On, a profile keeps the pages it visits, their icons, and its downloads. Off
// from the start it keeps none of them, and a page still shows its icon and a
// file still downloads. Turned off, what it kept goes, an open tab's back list
// with it, as in Chrome's Clear browsing data; its cookies and tabs stay, and
// nothing new comes, though a tab still goes back within itself. Turned on, it
// keeps pages again. The choice outlives a quit; a closed profile opens to
// clear when it is turned off; and a choice the engine lost to a crash right
// after the Save is made up, the clear with it, when the profile opens.

type Kept = { pages: string[]; icons: string[]; downloads: number };

const isEmpty = (kept: Kept) =>
	kept.pages.length === 0 && kept.icons.length === 0 && kept.downloads === 0;

const historyScenario: Scenario = async (context) => {
	const dataDir = context.tempDir("pane-history-");
	const scratch = context.tempDir("pane-history-copies-");
	const downloads = context.tempDir("pane-history-downloads-");

	const pageURL = (query: string) =>
		context.fixtures.url(`nav-a.html?${query}`);

	// One table of a copy of an engine database: the engine holds it locked
	// while it runs, and a copy with its journal reads as the engine left it.
	const rows = (profile: string, file: string, query: string): unknown[] => {
		const source = join(dataDir, "Engine", profile, file);

		if (!existsSync(source)) {
			return [];
		}

		const copy = join(scratch, `${profile}-${file}`);
		copyFileSync(source, copy);

		if (existsSync(`${source}-journal`)) {
			copyFileSync(`${source}-journal`, `${copy}-journal`);
		}

		const database = new Database(copy);

		try {
			return database.query(query).all();
		} finally {
			database.close();
		}
	};

	const keptBy = (profile: string): Kept => ({
		pages: (
			rows(profile, "History", "SELECT url FROM urls") as { url: string }[]
		).map((row) => row.url),
		icons: (
			rows(profile, "Favicons", "SELECT page_url FROM icon_mapping") as {
				page_url: string;
			}[]
		).map((row) => row.page_url),
		downloads: rows(profile, "History", "SELECT id FROM downloads").length,
	});

	// A page and a download in the profile; the page's tab.
	const browse = async (app: PaneApp, profile: string, query: string) => {
		const url = pageURL(query);
		const tab = await app.openTab(profile, url);
		await app.page(tab, url);

		await app.waitFor(
			`the icon of ${query}`,
			(state) => tabOf(state, tab)?.hasFavicon === true,
			15_000,
		);

		const before = (await app.state()).downloads.filter(
			(download) => download.state === "complete",
		).length;

		const fileURL = context.fixtures.url(`downloads.html?${query}`);
		const fileTab = await app.openTab(profile, fileURL);
		const filePage = await app.page(fileTab, fileURL);
		await app.send("setDownloadDirectory", { profile, path: downloads });
		await filePage.evaluate("document.getElementById('file').click()");

		await app.waitFor(
			`a download in ${query}`,
			(state) =>
				state.downloads.filter((download) => download.state === "complete")
					.length > before,
		);

		return tab;
	};

	// The engine commits within 10 s of a change: once one profile's write has
	// landed, the other's would have too.
	const commitInterval = 12_000;

	const engineOf = async (app: PaneApp, profile: string) =>
		(await app.state()).profiles.find((each) => each.id === profile)
			?.engineSavesHistory;

	const app = await launchPane(context, { dataDir });

	// Off from the start, browsing first; then the one that keeps.
	const off = (
		await app.send<string>("createProfile", {
			name: "Off",
			savesHistory: false,
		})
	).result;

	const keeps = await app.createProfile("Keeps");
	await browse(app, off, "off");
	const keepsTab = await browse(app, keeps, "keeps");

	const offEngine = await engineOf(app, off);
	const keepsEngine = await engineOf(app, keeps);

	expect(
		offEngine === false && keepsEngine === true,
		`the engine's choice: off ${offEngine}, keeps ${keepsEngine}`,
	);

	const keptWhileOn = await poll(
		"the profile's history on disk",
		() => {
			const kept = keptBy(keeps);

			return (
				kept.pages.some((page) => page.includes("keeps")) &&
				kept.icons.some((page) => page.includes("keeps")) &&
				kept.downloads > 0 &&
				kept
			);
		},
		30_000,
		500,
	);

	const keptWhileOff = keptBy(off);

	expect(
		isEmpty(keptWhileOff),
		`off from the start, it kept ${JSON.stringify(keptWhileOff)}`,
	);

	// A cookie, which clearing leaves, and a step of the tab's own history,
	// which it takes.
	const page = await app.page(keepsTab, pageURL("keeps"));
	await page.evaluate("document.cookie = 'pane_stays=1; max-age=3600'");
	await app.send("select", { tab: keepsTab });

	await app.send("navigate", {
		input: context.fixtures.url("nav-b.html?keeps"),
	});

	await app.waitFor(
		"the tab's next page",
		(state) =>
			tabOf(state, keepsTab)?.url.includes("nav-b.html") === true &&
			tabOf(state, keepsTab)?.canGoBack === true &&
			tabOf(state, keepsTab)?.loading === false,
	);

	// Turned off: what it kept goes.
	await app.send("setSavesHistory", { profile: keeps, savesHistory: false });

	await poll(
		"what it kept to go",
		() => {
			const kept = keptBy(keeps);

			return isEmpty(kept) && kept;
		},
		30_000,
		500,
	);

	await app.send("readCookies", { profile: keeps });

	const afterClear = await app.waitFor(
		"its cookies",
		(state) =>
			(state.profiles.find((each) => each.id === keeps)?.cookies?.length ?? 0) >
			0,
	);

	const afterTheClear = {
		cookie: afterClear.profiles
			.find((each) => each.id === keeps)
			?.cookies?.some((cookie) => cookie.name === "pane_stays"),
		tab: tabOf(afterClear, keepsTab)?.url.includes("nav-b.html"),
		canGoBack: tabOf(afterClear, keepsTab)?.canGoBack,
		engine: await engineOf(app, keeps),
	};

	expect(
		afterTheClear.cookie === true &&
			afterTheClear.tab === true &&
			afterTheClear.canGoBack === false &&
			afterTheClear.engine === false,
		`after the clear: ${JSON.stringify(afterTheClear)}`,
	);

	// Off, nothing new comes, and the tab still goes back within itself.
	await app.send("navigate", {
		input: context.fixtures.url("nav-a.html?within-the-tab"),
	});

	const goesBack = await app.waitFor(
		"the tab's own history while off",
		(state) =>
			tabOf(state, keepsTab)?.url.includes("within-the-tab") === true &&
			tabOf(state, keepsTab)?.canGoBack === true,
	);

	await browse(app, keeps, "after-off");
	await Bun.sleep(commitInterval);
	const keptAfterOff = keptBy(keeps);

	expect(
		isEmpty(keptAfterOff),
		`turned off, it kept ${JSON.stringify(keptAfterOff)}`,
	);

	// Turned on, it keeps pages again, in the open profile.
	await app.send("setSavesHistory", { profile: off, savesHistory: true });
	await browse(app, off, "back-on");

	const keptWhenBackOn = await poll(
		"the page kept again",
		() => {
			const kept = keptBy(off);

			return kept.pages.some((each) => each.includes("back-on")) && kept;
		},
		30_000,
		500,
	);

	await app.send("save");
	await app.browser.close();

	// A quit: each choice stands. A closed profile turned off opens, and
	// clears.
	const second = await launchPane(context, { dataDir });
	const reopened = await second.state();
	const closedEngine = await engineOf(second, off);

	expect(
		reopened.profiles.find((each) => each.id === off)?.savesHistory === true &&
			reopened.profiles.find((each) => each.id === keeps)?.savesHistory ===
				false,
		"the choices did not outlive the quit",
	);

	await second.send("setSavesHistory", { profile: off, savesHistory: false });

	await second.waitFor(
		"the closed profile to open",
		(state) =>
			state.profiles.find((each) => each.id === off)?.engineSavesHistory ===
			false,
		20_000,
	);

	await poll(
		"the closed profile's history to go",
		() => {
			const kept = keptBy(off);

			return isEmpty(kept) && kept;
		},
		30_000,
		500,
	);

	// Back on, with a page. Off again and a crash at once: the Save is on disk,
	// the profile's prefs, which the engine writes a few seconds on, are not.
	await second.send("setSavesHistory", { profile: off, savesHistory: true });
	await browse(second, off, "before-crash");

	await poll(
		"the page kept before the crash",
		() => keptBy(off).pages.some((each) => each.includes("before-crash")),
		30_000,
		500,
	);

	// The prefs carrying "on" have reached the disk by now.
	await Bun.sleep(commitInterval);
	await second.send("setSavesHistory", { profile: off, savesHistory: false });
	await second.send("save");

	const crashed = second.browser.pid;
	process.kill(crashed, "SIGKILL");
	await poll("the crash", () => !isRunning(crashed), 10_000);

	const third = await launchTraced(context, "profile-history", { dataDir });
	const url = pageURL("after-crash");
	await third.app.page(await third.app.openTab(off, url), url);

	const madeUp = await third.waitForEntry(
		"PaneBrowserContext.setSavesHistory",
		(args) => args.savesHistory === false,
	);

	const keptAfterCrash = await poll(
		"what it kept to go, once it opened",
		() => {
			const kept = keptBy(off);

			return isEmpty(kept) && kept;
		},
		30_000,
		500,
	);

	await third.app.browser.close();

	return {
		keptWhileOn,
		keptWhileOff,
		afterTheClear,
		goesBackWhileOff: tabOf(goesBack, keepsTab)?.canGoBack,
		keptAfterOff,
		keptWhenBackOn,
		closedEngineAtLaunch: closedEngine,
		madeUpAtOpen: madeUp.args,
		keptAfterCrash,
	};
};

export default historyScenario;
