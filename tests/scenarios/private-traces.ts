import {
	chmodSync,
	existsSync,
	readdirSync,
	readFileSync,
	statSync,
} from "node:fs";
import { join } from "node:path";
import { $ } from "bun";
import { launchPane, tabOf } from "../lib/pane";
import { expect, type Scenario } from "../run";

// What Pane leaves outside the encrypted volume (ADR 0113) names no page. A
// renderer's crash leaves no minidump: Chromium's Crashpad kept one, with the
// page's address (url-chunk), under its default user data dir. A download
// keeps its quarantine and no address: Chromium wrote the page and the file's
// URLs into kMDItemWhereFroms, which Spotlight indexes. After a quit, Local
// State names no profile of Pane's as used last, and keeps no time of use.
// The data root is the user's alone, where it was 0755. The engine keeps no
// histograms (BrowserMetrics), which counted the pages loaded and the tabs
// open (ADR 0115).
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
	const dataDir = context.tempDir("pane-traces-");
	chmodSync(dataDir, 0o755);

	// Their histograms are not seeded: Chrome, finding some, empties the folder,
	// its own file too.
	const engine = join(dataDir, "Engine");

	const app = await launchPane(context, { dataDir });
	// Chrome makes the histograms' file as it starts, and may delete it a
	// few seconds on: it is looked for at once, and again before the quit.
	const histogramsAtLaunch = histogramFiles(engine);
	const home = join(dataDir, "home");
	const profile = await app.createProfile("Traces");

	// A download.
	const url = context.fixtures.url("downloads.html");
	const tab = await app.openTab(profile, url);
	const page = await app.page(tab, url);
	const directory = context.tempDir("pane-traces-downloads-");
	await app.send("setDownloadDirectory", { profile, path: directory });
	await page.evaluate("document.getElementById('file').click()");

	const done = await app.waitFor("a completed download", (state) =>
		state.downloads.some((download) => download.state === "complete"),
	);

	const saved =
		done.downloads.find((download) => download.state === "complete")?.path ??
		"";

	const attributes = (await $`/usr/bin/xattr ${saved}`.quiet().nothrow().text())
		.trim()
		.split("\n")
		.filter(Boolean);

	// A renderer's crash, which the app reloads.
	const crashURL = context.fixtures.url("crash.html");
	const crashTab = await app.openTab(profile, crashURL);
	const crashPage = await app.page(crashTab, crashURL);
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

	const dumps = (
		await $`/usr/bin/find ${home} ${dataDir} -name ${"*.dmp"}`
			.quiet()
			.nothrow()
			.text()
	)
		.trim()
		.split("\n")
		.filter(Boolean);

	const localState = JSON.parse(
		readFileSync(join(dataDir, "Engine", "Local State"), "utf8"),
	) as {
		profile?: {
			last_used?: string;
			info_cache?: Record<string, { active_time?: number }>;
		};
		tab_stats?: unknown;
	};

	const traces = {
		downloadAttributes: attributes,
		dumps,
		crashpad: existsSync(
			join(home, "Library", "Application Support", "Chromium", "Crashpad"),
		)
			? readdirSync(
					join(home, "Library", "Application Support", "Chromium", "Crashpad"),
				)
			: [],
		lastUsed: localState.profile?.last_used,
		timesOfUse: Object.values(localState.profile?.info_cache ?? {}).filter(
			(entry) => entry.active_time !== undefined,
		).length,
		tabStats: localState.tab_stats !== undefined,
		dataRootMode: (statSync(dataDir).mode & 0o777).toString(8),
		histograms: [
			...new Set([
				...histogramsAtLaunch,
				...histogramsWhileOpen,
				...histogramFiles(engine),
			]),
		],
	};

	expect(
		attributes.includes("com.apple.quarantine") &&
			!attributes.includes("com.apple.metadata:kMDItemWhereFroms"),
		`the download's attributes: ${JSON.stringify(attributes)}`,
	);

	expect(dumps.length === 0, `minidumps: ${JSON.stringify(dumps)}`);

	expect(
		traces.lastUsed === "Default" &&
			traces.timesOfUse === 0 &&
			!traces.tabStats,
		`Local State after the quit: ${JSON.stringify(traces)}`,
	);

	expect(
		traces.dataRootMode === "700",
		`the data root is ${traces.dataRootMode}`,
	);

	expect(
		traces.histograms.length === 0,
		`histograms ${JSON.stringify(traces.histograms)}`,
	);
};

export default privateTraces;
