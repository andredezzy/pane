import {
	chmodSync,
	existsSync,
	mkdirSync,
	readdirSync,
	readFileSync,
	statSync,
	writeFileSync,
} from "node:fs";
import { join } from "node:path";
import { EARLIER_APPS, launchPane, tabOf } from "../lib/pane";
import { ENGINE_FOLDER } from "../lib/platform";
import {
	crashDumpsUnder,
	crashpadKept,
	dataFolderPrivacy,
	downloadOrigin,
} from "../lib/tools";
import { expect, type Scenario } from "../run";

// What Pane leaves outside the encrypted volume (ADR 0113), and on Windows,
// which has none, outside its files' encryption (U5), names no page. A
// renderer's crash leaves no minidump, and Crashpad's database nothing:
// Chromium's Crashpad kept one, with the page's address (url-chunk), under
// its default user data dir, which Pane starts no handler for. A download
// keeps its system's mark and no address: Chromium wrote the page and the
// file's URLs into kMDItemWhereFroms, which Spotlight indexes, and into the
// Mark of the Web on Windows (lib/tools.ts). After a quit, Local State names
// no profile of Pane's as used last, and keeps no time of use. The data root
// is the user's alone: on the Mac where it was 0755; on Windows by the ACL of
// the user's profile it lies in. The engine keeps no histograms
// (BrowserMetrics), which counted the pages loaded and the tabs open, and on
// the Mac the extensions an earlier migration record listed go at launch
// (ADR 0115).
// What Chrome keeps of its histograms at the engine's root.
const histogramFiles = (engine: string) =>
	["BrowserMetrics", "BrowserMetrics-spare.pma", "DeferredBrowserMetrics"]
		.filter((name) => existsSync(join(engine, name)))
		.flatMap((name) =>
			statSync(join(engine, name)).isDirectory()
				? readdirSync(join(engine, name)).map((file) => `${name}/${file}`)
				: [name],
		);

const privateTraces: Scenario = async (context) => {
	// Named as the user's is, so it runs as the user's launch too
	// (--user-launch).
	const dataDir = join(context.tempDir("pane-traces-"), "Pane");
	mkdirSync(dataDir);
	// Windows reads no mode but its read-only flag: its ACL is the profile's.
	chmodSync(dataDir, 0o755);

	// A record 0.15.1 and before left, listing the extensions each profile
	// had (an earlier app's: the Mac's alone). Their histograms are not
	// seeded: Chrome, finding some, empties the folder, its own file too (the
	// volume's tests erase them).
	const engine = join(dataDir, ENGINE_FOLDER);

	if (EARLIER_APPS) {
		writeFileSync(
			join(dataDir, "migration.json"),
			JSON.stringify({
				state: {
					source: "/nowhere/@pane/veil",
					migratedAt: "2026-10-28T00:00:00Z",
					profiles: [],
					pendingExtensions: { a: ["cjpalhdlnbpafiamejdnhcphjbkeiagm"] },
				},
				version: 0,
			}),
		);
	}

	const launched = Date.now();
	const app = await launchPane(context, { dataDir });
	// Chrome makes the histograms' file as it starts, and may delete it a
	// few seconds on: it is looked for at once, and again before the quit.
	const histogramsAtLaunch = histogramFiles(engine);
	const home = join(dataDir, "home");
	const profile = await app.createProfile("Traces");

	// A download.
	const url = context.fixtures.url("downloads.html");
	const { page } = await app.openPage(profile, url);
	const directory = context.tempDir("pane-traces-downloads-");
	await app.send("setDownloadDirectory", { profile, path: directory });
	await page.evaluate("document.getElementById('file').click()");

	const done = await app.waitFor("a completed download", (state) =>
		state.downloads.some((download) => download.state === "complete"),
	);

	const saved =
		done.downloads.find((download) => download.state === "complete")?.path ??
		"";

	const origin = await downloadOrigin(saved);

	// A renderer's crash, which the app reloads.
	const crashURL = context.fixtures.url("crash.html");

	const { tab: crashTab, page: crashPage } = await app.openPage(
		profile,
		crashURL,
	);

	crashPage.send("Page.crash").catch(() => {});

	await app.waitFor(
		"the crashed tab reloaded",
		(state) =>
			tabOf(state, crashTab)?.loading === false &&
			tabOf(state, crashTab)?.url === crashURL,
	);

	// Long enough for a crash handler to have written its dump, were there
	// one.
	await Bun.sleep(2_000);

	// And while Pane runs: Chrome deletes it at some quits and not at
	// others.
	const histogramsWhileOpen = histogramFiles(engine);
	await app.browser.close();

	const dumps = await crashDumpsUnder([home, dataDir]);

	const localState = JSON.parse(
		readFileSync(join(engine, "Local State"), "utf8"),
	) as {
		profile?: {
			last_used?: string;
			info_cache?: Record<string, { active_time?: number }>;
		};
		tab_stats?: unknown;
	};

	const privacy = await dataFolderPrivacy(dataDir);

	const traces = {
		download: origin.kept,
		dumps,
		crashpad: crashpadKept({ home, dataDir }, launched),
		lastUsed: localState.profile?.last_used,
		timesOfUse: Object.values(localState.profile?.info_cache ?? {}).filter(
			(entry) => entry.active_time !== undefined,
		).length,
		tabStats: localState.tab_stats !== undefined,
		dataRoot: privacy.kept,
		histograms: [
			...new Set([
				...histogramsAtLaunch,
				...histogramsWhileOpen,
				...histogramFiles(engine),
			]),
		],
		pendingExtensions:
			EARLIER_APPS &&
			readFileSync(join(dataDir, "migration.json"), "utf8").includes(
				"pendingExtensions",
			),
	};

	expect(
		origin.marked && !origin.namesAPage,
		`what the download keeps of where it came from: ${JSON.stringify(origin.kept)}`,
	);

	expect(dumps.length === 0, `minidumps: ${JSON.stringify(dumps)}`);

	expect(
		traces.crashpad.length === 0,
		`Crashpad's database: ${JSON.stringify(traces.crashpad)}`,
	);

	expect(
		traces.lastUsed === "Default" &&
			traces.timesOfUse === 0 &&
			!traces.tabStats,
		`Local State after the quit: ${JSON.stringify(traces)}`,
	);

	expect(privacy.alone, `the data root is ${privacy.kept}`);

	expect(
		traces.histograms.length === 0 && !traces.pendingExtensions,
		`histograms ${JSON.stringify(traces.histograms)}, pending extensions ${traces.pendingExtensions}`,
	);
};

export default privateTraces;
