import { stats } from "../lib/benchmark";
import type { Page } from "../lib/cdp";
import { workerPage } from "../lib/extensions";
import { launchPane, type PaneApp, type PaneLaunch } from "../lib/pane";
import { sessionFolders } from "../lib/rich-session";
import type { Scenario } from "../run";

// What a store's own format costs (docs/benchmarks/engine-encryption.md), on
// data big enough to reach the disk: a query of a filled History, the first
// request after a relaunch with thousands of cookies to load, IndexedDB's writes
// and reads, Local Storage's first read, and the repeat visit, a page loaded
// again after a relaunch from what the last launch cached.

const RELAUNCHES = 5;
const HISTORY_ENTRIES = 5_000;
const HISTORY_QUERIES = 25;
const COOKIES = 3_000;
const RECORDS = 2_000;
const RECORD_BYTES = 1_024;
const BLOB_BYTES = 8 * 1024 * 1024;
const LOCAL_STORAGE_ITEMS = 2_000;
const STORES_COMMITTED_MS = 12_000;

const SITES = [
	"https://en.wikipedia.org/wiki/Web_browser",
	"https://www.apple.com/",
	"https://github.com/",
];

const percentile = (values: number[], share: number) => {
	const sorted = [...values].sort((a, b) => a - b);

	return sorted[Math.min(sorted.length - 1, Math.floor(sorted.length * share))];
};

const summary = (values: number[]) => ({
	...stats(values),
	p95: percentile(values, 0.95),
});

const EXTENSION_NAME = "Pane rich session probe";

async function extensionWorker(app: PaneApp, profile: string, url: string) {
	const state = await app.waitFor("the probe extension", (current) =>
		(current.extensions[profile] ?? []).some(
			(item) => item.name === EXTENSION_NAME,
		),
	);

	const id = (state.extensions[profile] ?? []).find(
		(item) => item.name === EXTENSION_NAME,
	)?.id as string;

	return workerPage(app, id, url);
}

// The first navigation's time to its first byte: cookies load before its
// request is made.
const navigationTiming = (page: Page) =>
	page.evaluate<{
		responseStart: number;
		load: number;
	}>(`new Promise((resolve) => {
		const done = () => {
			const entry = performance.getEntriesByType("navigation")[0];
			resolve({ responseStart: entry.responseStart, load: entry.loadEventEnd });
		};
		if (document.readyState === "complete") { setTimeout(done, 0); } else { addEventListener("load", () => setTimeout(done, 0)); }
	})`);

const benchmarkStores: Scenario = async (context) => {
	const folders = sessionFolders(context);
	const launch: PaneLaunch = folders.launch;
	const { port } = new URL(context.fixtures.origin);
	const origin = `http://stores.localhost:${port}`;
	const page = `${origin}/smoke.html`;

	const first = await launchPane(context, launch);
	const profile = await first.createProfile("Stores");
	const tab = await first.openTab(profile, page);
	const storePage = await first.page(tab, page);
	const worker = await extensionWorker(first, profile, page);

	// History: filled through the extension API, which writes as a visit does.
	await worker.evaluate(`(async () => {
		for (let start = 0; start < ${HISTORY_ENTRIES}; start += 100) {
			await Promise.all(Array.from({ length: 100 }, (_, i) =>
				chrome.history.addUrl({ url: "https://history-" + (start + i) + ".example/needle/" + (start + i) })));
		}
	})()`);

	// Cookies, on one site and its subdomains, as many sites' would load.
	await worker.evaluate(`(async () => {
		for (let start = 0; start < ${COOKIES}; start += 100) {
			await Promise.all(Array.from({ length: 100 }, (_, i) =>
				chrome.cookies.set({ url: ${JSON.stringify(`${origin}/`)}, name: "c" + (start + i), value: "v".repeat(64), expirationDate: Date.now() / 1000 + 86400 })));
		}
	})()`);

	// IndexedDB's writes and reads, and a blob's.
	const indexedDB = await storePage.evaluate<{
		put: number;
		get: number;
		blobPut: number;
		blobGet: number;
	}>(`(async () => {
		const open = () => new Promise((resolve, reject) => {
			const opened = indexedDB.open("stores", 1);
			opened.onupgradeneeded = () => opened.result.createObjectStore("items");
			opened.onerror = () => reject(opened.error);
			opened.onsuccess = () => resolve(opened.result);
		});
		const db = await open();
		const done = (tx) => new Promise((resolve, reject) => { tx.oncomplete = resolve; tx.onerror = () => reject(tx.error); });
		const value = "r".repeat(${RECORD_BYTES});
		let start = performance.now();
		let tx = db.transaction("items", "readwrite");
		for (let i = 0; i < ${RECORDS}; i++) { tx.objectStore("items").put(value + i, "k" + i); }
		await done(tx);
		const put = performance.now() - start;
		start = performance.now();
		tx = db.transaction("items");
		for (let i = 0; i < ${RECORDS}; i++) { tx.objectStore("items").get("k" + i); }
		await done(tx);
		const get = performance.now() - start;
		start = performance.now();
		tx = db.transaction("items", "readwrite");
		tx.objectStore("items").put(new Blob([new Uint8Array(${BLOB_BYTES})]), "blob");
		await done(tx);
		const blobPut = performance.now() - start;
		start = performance.now();
		const blob = await new Promise((resolve, reject) => {
			const read = db.transaction("items").objectStore("items").get("blob");
			read.onsuccess = () => resolve(read.result);
			read.onerror = () => reject(read.error);
		});
		await blob.arrayBuffer();
		const blobGet = performance.now() - start;
		db.close();
		return { put, get, blobPut, blobGet };
	})()`);

	await storePage.evaluate(`(() => {
		for (let i = 0; i < ${LOCAL_STORAGE_ITEMS}; i++) { localStorage.setItem("k" + i, "v".repeat(256)); }
	})()`);

	// The sites' pages, for the repeat visits after a relaunch to load from.
	const visit = async (app: PaneApp, url: string) => {
		const opened = await app.openTab(profile, url);
		const loaded = await app.page(opened, url);
		const timing = await navigationTiming(loaded);
		await app.send("closeTab", { tab: opened });

		return timing;
	};

	const firstVisit: Record<string, number> = {};

	for (const site of SITES) {
		const timing = await visit(first, site).catch(() => null);

		if (timing) {
			firstVisit[site] = timing.load;
		}
	}

	await Bun.sleep(STORES_COMMITTED_MS);
	await first.browser.close();

	const queries: number[] = [];
	const firstRequest: number[] = [];
	const localStorageFirstRead: number[] = [];
	const repeat: Record<string, number[]> = {};

	for (let run = 0; run < RELAUNCHES; run++) {
		const app = await launchPane(context, launch);
		const url = `${page}?relaunch=${run}`;
		const opened = await app.openTab(profile, url);
		const loaded = await app.page(opened, url);
		firstRequest.push((await navigationTiming(loaded)).responseStart);

		localStorageFirstRead.push(
			await loaded.evaluate<number>(`(() => {
				const start = performance.now();
				localStorage.getItem("k0");
				return performance.now() - start;
			})()`),
		);

		const probe = await extensionWorker(app, profile, url);

		for (let query = 0; query < HISTORY_QUERIES; query++) {
			queries.push(
				await probe.evaluate<number>(`(async () => {
					const start = performance.now();
					await chrome.history.search({ text: "needle/${(query + 1) * 37}", startTime: 0, maxResults: 100 });
					return performance.now() - start;
				})()`),
			);
		}

		for (const site of SITES) {
			const timing = await visit(app, site).catch(() => null);

			if (timing) {
				repeat[site] = [...(repeat[site] ?? []), timing.load];
			}
		}

		await app.browser.close();
		await Bun.sleep(1_000);
	}

	return {
		sizes: {
			historyEntries: HISTORY_ENTRIES,
			cookies: COOKIES,
			records: RECORDS,
			recordBytes: RECORD_BYTES,
			blobBytes: BLOB_BYTES,
			localStorageItems: LOCAL_STORAGE_ITEMS,
		},
		historyQueryMs: summary(queries),
		firstRequestWithCookiesMs: summary(firstRequest),
		localStorageFirstReadMs: summary(localStorageFirstRead),
		indexedDB,
		firstVisitMs: firstVisit,
		repeatVisitMs: Object.fromEntries(
			Object.entries(repeat).map(([site, runs]) => [site, summary(runs)]),
		),
	};
};

export default benchmarkStores;
