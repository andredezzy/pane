import { Database } from "bun:sqlite";
import { copyFileSync, existsSync } from "node:fs";
import { join } from "node:path";
import { $ } from "bun";
import { launchPane, type PaneApp, tabOf } from "../lib/pane";
import { poll } from "../lib/poll";
import { isRunning } from "../lib/processes";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// A profile's Save history (ADR 0119), against what the engine writes to disk.
// On, a profile keeps the pages it visits, their icons, its downloads, and what
// Chrome learns from them. Off from the start it keeps none of them, an
// extension's chrome.history.addUrl included, and a page still shows its icon
// and a file still downloads: no file of the profile but its site data names a
// site it went to, each visit having a host of its own to look for. Turned off,
// what it kept goes, an open tab's back list with it, as in Chrome's Clear
// browsing data; its cookies and tabs stay, and nothing new comes, though a tab
// still goes back within itself. Turned on, it keeps pages again. The choice
// outlives a quit, with nothing committed as its pages close; a closed profile opens to
// clear when it is turned off, and closes again; a choice the engine lost to a
// crash right after the Save is made up, the clear with it, when the profile
// opens; and so is a clear a lock or a quit cut short, with the choice landed.

type Kept = { pages: string[]; icons: string[]; downloads: number };

const isEmpty = (kept: Kept) =>
	kept.pages.length === 0 && kept.icons.length === 0 && kept.downloads === 0;

// What keeps a site by name on purpose: the profile's site data (ADR 0119).
const SITE_DATA = /^\/(Cache|Code Cache|Cookies|Local Storage|Session Storage)/;

// The downloads a profile ran before it was turned off, which a deletion leaves
// in this log until the profile next opens.
const DOWNLOAD_LOG = /^\/shared_proto_db\//;

const historyScenario: Scenario = async (context) => {
	const dataDir = context.tempDir("pane-history-");
	const scratch = context.tempDir("pane-history-copies-");
	const downloads = context.tempDir("pane-history-downloads-");

	// Each visit on a host of its own, as the records by site name it. Any host
	// under pane.test is the fixtures' server.
	const { port } = new URL(context.fixtures.origin);
	const RESOLVER = "--host-resolver-rules=MAP *.pane.test 127.0.0.1";

	const fixtureURL = (page: string, query: string) =>
		`http://${query}.pane.test:${port}/${page}?${query}`;

	const pageURL = (query: string) => fixtureURL("nav-a.html", query);

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

	// Every file of the profile that names the visit's host, its site data aside.
	const namedBy = async (profile: string, query: string) => {
		const root = join(dataDir, "Engine", profile);

		const files =
			await $`find -L ${root} -type f -print0 | xargs -0 /usr/bin/grep -la ${`${query}.pane.test`}`
				.quiet()
				.nothrow()
				.text();

		return files
			.split("\n")
			.filter(Boolean)
			.map((file) => file.slice(root.length))
			.filter((file) => !SITE_DATA.test(file));
	};

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

		const fileURL = fixtureURL("downloads.html", query);
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

	const PROBE = join(
		import.meta.dir,
		"..",
		"pages",
		"extensions",
		"history-probe",
	);

	const { app, has } = await launchTraced(context, "profile-history-first", {
		dataDir,
		args: [RESOLVER, `--load-extension=${PROBE}`],
	});

	// Off from the start, browsing first; then the one that keeps.
	const off = (
		await app.send<string>("createProfile", {
			name: "Off",
			savesHistory: false,
		})
	).result;

	const keeps = await app.createProfile("Keeps");
	await browse(app, off, "off");

	// An extension asks to add a page, in each.
	const addPage = async (profile: string, added: string) => {
		const loaded = await app.waitFor("the probe extension", (state) =>
			(state.extensions[profile] ?? []).some(
				(item) => item.name === "Pane history probe",
			),
		);

		const id = (loaded.extensions[profile] ?? []).find(
			(item) => item.name === "Pane history probe",
		)?.id;

		const url = `chrome-extension://${id}/probe.html?${added}`;
		const page = await app.page(await app.openTab(profile, url), url);

		await page.evaluate(
			`chrome.history.addUrl({ url: ${JSON.stringify(added)} })`,
		);
	};

	await addPage(off, "https://added-off.example/");
	const keepsTab = await browse(app, keeps, "keeps");
	await addPage(keeps, "https://added-keeps.example/");

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
				kept.pages.includes(pageURL("keeps")) &&
				kept.pages.includes("https://added-keeps.example/") &&
				kept.icons.includes(pageURL("keeps")) &&
				kept.downloads > 0 &&
				kept
			);
		},
		30_000,
		500,
	);

	// What the records by site hold on, once they have reached the disk: the
	// proof that what follows would see them.
	const namedWhileOn = await poll(
		"the records of its visits on disk",
		async () => {
			const named = await namedBy(keeps, "keeps");

			return (
				["/History", "/Preferences", "/Network Action Predictor"].every(
					(file) => named.includes(file),
				) && named
			);
		},
		30_000,
		500,
	);

	const keptWhileOff = keptBy(off);
	const namedWhileOff = await namedBy(off, "off");

	expect(
		isEmpty(keptWhileOff) && namedWhileOff.length === 0,
		`off from the start, it kept ${JSON.stringify(keptWhileOff)} in ${JSON.stringify(namedWhileOff)}`,
	);

	// With nothing kept, nothing to clear.
	expect(
		!has("PaneBrowserContext.clearHistory"),
		"a new profile, off from the start, ran a clear as it opened",
	);

	// A cookie, which clearing leaves, and a step of the tab's own history,
	// which it takes.
	const page = await app.page(keepsTab, pageURL("keeps"));
	await page.evaluate("document.cookie = 'pane_stays=1; max-age=3600'");
	await app.send("select", { tab: keepsTab });

	await app.send("navigate", {
		input: fixtureURL("nav-b.html", "keeps"),
	});

	await app.waitFor(
		"the tab's next page",
		(state) =>
			tabOf(state, keepsTab)?.url.includes("nav-b.html") === true &&
			tabOf(state, keepsTab)?.canGoBack === true &&
			tabOf(state, keepsTab)?.loading === false,
	);

	// A tab closed in each, which Reopen closed tab holds in memory.
	for (const [profile, query] of [
		[keeps, "closed-here"],
		[off, "closed-elsewhere"],
	] as const) {
		const closing = await app.openTab(profile, pageURL(query));
		await app.send("closeTab", { tab: closing });
	}

	await app.waitFor("two closed tabs", (state) => state.closedTabs === 2);
	await app.send("select", { tab: keepsTab });

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

	await app.waitFor(
		"the closed tab of the profile that cleared to go, and the other to stay",
		(state) => state.closedTabs === 1,
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
		input: pageURL("within-the-tab"),
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

	const namedAfterOff = [
		...(await namedBy(keeps, "keeps")).filter(
			(file) => !DOWNLOAD_LOG.test(file),
		),
		...(await namedBy(keeps, "after-off")),
		...(await namedBy(keeps, "within-the-tab")),
	];

	expect(
		isEmpty(keptAfterOff) && namedAfterOff.length === 0,
		`turned off, it kept ${JSON.stringify(keptAfterOff)} in ${JSON.stringify(namedAfterOff)}`,
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

	// A quit: each choice stands, and the pages that closed with it, opened
	// while their profile kept its history, commit nothing. Opening, the profile
	// leaves the log behind. A closed profile turned off opens, and clears.
	const second = await launchPane(context, { dataDir, args: [RESOLVER] });
	const reopened = await second.state();
	const closedEngine = await engineOf(second, off);
	const quitURL = pageURL("after-quit");
	await second.page(await second.openTab(keeps, quitURL), quitURL);
	const namedAfterQuit = await namedBy(keeps, "keeps");

	expect(
		namedAfterQuit.length === 0,
		`after the quit, it kept ${JSON.stringify(namedAfterQuit)}`,
	);

	expect(
		reopened.profiles.find((each) => each.id === off)?.savesHistory === true &&
			reopened.profiles.find((each) => each.id === keeps)?.savesHistory ===
				false,
		"the choices did not outlive the quit",
	);

	await second.send("setSavesHistory", { profile: off, savesHistory: false });

	await poll(
		"the closed profile's history to go",
		() => {
			const kept = keptBy(off);

			return isEmpty(kept) && kept;
		},
		30_000,
		500,
	);

	await second.waitFor(
		"the profile opened to clear to close again",
		(state) =>
			state.profiles.find((each) => each.id === off)?.engineSavesHistory ===
			null,
		20_000,
	);

	// Owed until the engine's commit has reached the disk: 15 s on.
	await poll(
		"the clear no longer owed",
		async () =>
			!(
				await second.send<{ historyClearOwed: boolean }>("readProfile", {
					profile: off,
				})
			).result.historyClearOwed,
		25_000,
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

	const third = await launchTraced(context, "profile-history", {
		dataDir,
		args: [RESOLVER],
	});

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

	// The choice landed and the clear did not, as a lock or a quit can leave
	// it: the engine's pref already says off, and the clear still runs.
	await third.app.send("setClearsHistory", { profile: off });
	await third.app.send("save");
	await third.app.browser.close();

	const fourth = await launchTraced(context, "profile-history-owed", {
		dataDir,
		args: [RESOLVER],
	});

	const owedURL = pageURL("after-owed");
	await fourth.app.page(await fourth.app.openTab(off, owedURL), owedURL);

	const owedClear = await fourth.waitForEntry(
		"PaneBrowserContext.clearHistory",
	);

	const owedGone = await poll(
		"the owed clear to finish",
		async () =>
			!(
				await fourth.app.send<{ historyClearOwed: boolean }>("readProfile", {
					profile: off,
				})
			).result.historyClearOwed,
		25_000,
		500,
	);

	await fourth.app.browser.close();

	return {
		keptWhileOn,
		namedWhileOn,
		keptWhileOff,
		afterTheClear,
		goesBackWhileOff: tabOf(goesBack, keepsTab)?.canGoBack,
		keptAfterOff,
		keptWhenBackOn,
		closedEngineAtLaunch: closedEngine,
		madeUpAtOpen: madeUp.args,
		owedClearAtOpen: { ran: owedClear.name, done: owedGone },
		keptAfterCrash,
	};
};

export default historyScenario;
