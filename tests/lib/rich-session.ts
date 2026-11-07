// A rich session on throwaway data (docs/adr/0121): a
// profile signed in, with history, Local and Session Storage, IndexedDB with
// blobs, CacheStorage, a service worker, the origin private file system with a
// sync access handle, a download, and an extension, each holding a value of its
// own, random, for files-encrypted to look for on disk.
import { mkdirSync, realpathSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import type { ScenarioContext } from "../run";
import type { Page } from "./cdp";
import { extensionId, workerPage } from "./extensions";
import type { AuditRoots, Planted } from "./file-audit";
import { launchPane, type PaneApp, type PaneLaunch } from "./pane";

const EXTENSION = join(
	import.meta.dir,
	"..",
	"pages",
	"extensions",
	"rich-session",
);

const plant = () => crypto.randomUUID().replaceAll("-", "");

export type RichSession = {
	app: PaneApp;
	profile: string;
	tab: string;
	page: Page;
	url: string;
	planted: Planted;
	roots: AuditRoots;
	// Its throwaway home, where it can launch Pane again.
	launch: PaneLaunch;
};

// The data root under a throwaway home, so the caches' folder is the link a
// Mac's is (`derivedDataRoot`), and a temporary folder of its own, so what
// Pane's processes make there is theirs alone.
export function sessionFolders(context: ScenarioContext, binary?: string) {
	const home = realpathSync(context.tempDir("pane-rich-home-"));

	const dataDir = join(
		home,
		"Library",
		"Application Support",
		"xyz.roundedcorners.pane",
	);

	mkdirSync(dataDir, { recursive: true });

	const temporary = join(home, "tmp");
	mkdirSync(temporary, { recursive: true });

	const launch: PaneLaunch = {
		...(binary && { binary }),
		dataDir,
		derivedDataRoot: true,
		env: { HOME: home, CFFIXED_USER_HOME: home, TMPDIR: `${temporary}/` },
		args: [`--load-extension=${EXTENSION}`],
	};

	return { home, dataDir, temporary, launch };
}

// What the session's page does in the page, each value kept where its name says.
const STORE = (values: Record<string, string>) => `(async () => {
	const values = ${JSON.stringify(values)};
	localStorage.setItem("rich", values.localStorage);
	sessionStorage.setItem("rich", values.sessionStorage);
	document.cookie = "rich_script=" + values.scriptCookie + "; max-age=86400; path=/";

	await new Promise((resolve, reject) => {
		const opened = indexedDB.open("rich-session", 1);
		opened.onupgradeneeded = () => opened.result.createObjectStore("items");
		opened.onerror = () => reject(opened.error);
		opened.onsuccess = () => {
			const tx = opened.result.transaction("items", "readwrite");
			const items = tx.objectStore("items");
			items.put(values.indexedDB, "value");
			// Past the size IndexedDB keeps in its LevelDB: a file of its own.
			items.put(new Blob([values.indexedDBBlob, "x".repeat(256 * 1024)], { type: "text/plain" }), "blob");
			tx.oncomplete = () => { opened.result.close(); resolve(); };
			tx.onerror = () => reject(tx.error);
		};
	});

	const cache = await caches.open("rich-session");
	await cache.put("rich-session-entry", new Response(values.cacheStorage));

	await navigator.serviceWorker.register("/rich-session/worker.js?value=" + values.serviceWorker);
	const registration = await navigator.serviceWorker.ready;

	const root = await navigator.storage.getDirectory();
	const file = await root.getFileHandle("rich-session.txt", { create: true });
	const writable = await file.createWritable();
	await writable.write(values.originPrivate);
	await writable.close();

	const worker = new Worker("/rich-session-opfs-worker.js");
	const read = await new Promise((resolve, reject) => {
		worker.onmessage = ({ data }) => resolve(data);
		worker.onerror = (event) => reject(new Error(event.message));
		worker.postMessage({ value: values.originPrivateSync });
	});

	return { worker: registration.active !== null || registration.installing !== null, read };
})()`;

// What the page reads back: for a check after a relaunch, a lock, or a migration.
export const READ_BACK = `(async () => {
	const fromIndexedDB = await new Promise((resolve, reject) => {
		const opened = indexedDB.open("rich-session", 1);
		opened.onerror = () => reject(opened.error);
		opened.onsuccess = () => {
			const items = opened.result.transaction("items").objectStore("items");
			const value = items.get("value");
			const blob = items.get("blob");
			blob.onsuccess = async () => {
				const text = blob.result ? await blob.result.text() : "";
				opened.result.close();
				resolve({ value: value.result ?? null, blob: text.slice(0, 32), blobSize: blob.result?.size ?? 0 });
			};
		};
	});
	const cached = await (await caches.open("rich-session")).match("rich-session-entry");
	const root = await navigator.storage.getDirectory();
	const file = await (await root.getFileHandle("rich-session.txt")).getFile();
	const sync = await (await root.getFileHandle("rich-session-sync.txt")).getFile();
	const worker = await navigator.serviceWorker.getRegistration();
	return {
		localStorage: localStorage.getItem("rich"),
		sessionStorage: sessionStorage.getItem("rich"),
		cookie: document.cookie,
		indexedDB: fromIndexedDB,
		cacheStorage: cached ? await cached.text() : null,
		originPrivate: await file.text(),
		originPrivateSync: await sync.text(),
		serviceWorker: worker?.active?.scriptURL ?? null,
	};
})()`;

export type ReadBack = {
	localStorage: string | null;
	sessionStorage: string | null;
	cookie: string;
	indexedDB: { value: string | null; blob: string; blobSize: number };
	cacheStorage: string | null;
	originPrivate: string;
	originPrivateSync: string;
	serviceWorker: string | null;
};

export async function richSession(
	context: ScenarioContext,
	binary?: string,
): Promise<RichSession> {
	const folders = sessionFolders(context, binary);
	const app = await launchPane(context, folders.launch);
	const profile = await app.createProfile("Rich session");

	const values = {
		httpOnlyCookie: plant(),
		scriptCookie: plant(),
		pageText: plant(),
		script: plant(),
		localStorage: plant(),
		sessionStorage: plant(),
		indexedDB: plant(),
		indexedDBBlob: plant(),
		cacheStorage: plant(),
		serviceWorker: plant(),
		originPrivate: plant(),
		originPrivateSync: plant(),
		extensionStorage: plant(),
		query: plant(),
	};

	// A host of its own, a secure context by Chromium's rule for *.localhost,
	// as a real site's https is: CacheStorage, the service worker, and the
	// origin private file system need one.
	const { port } = new URL(context.fixtures.origin);
	const host = `rich-${plant().slice(0, 16)}.localhost`;
	const origin = `http://${host}:${port}`;

	const url = `${origin}/rich-session/page?q=${values.query}&text=${values.pageText}&cookie=${values.httpOnlyCookie}&script=${values.script}`;
	const tab = await app.openTab(profile, url);
	const page = await app.page(tab, url);
	await page.evaluate(STORE(values));

	// The extension's own storage, its LevelDB, and its service worker.
	const extensionID = await extensionId(
		app,
		profile,
		"Pane rich session probe",
	);

	const worker = await workerPage(app, extensionID, url);

	await worker.evaluate(
		`chrome.storage.local.set({ rich: ${JSON.stringify(values.extensionStorage)} })`,
	);

	// A download through the page, as a click on a link.
	const directory = join(folders.home, "Downloads");
	mkdirSync(directory, { recursive: true });
	await app.send("setDownloadDirectory", { profile, path: directory });

	await page.evaluate(
		`(() => { const a = document.createElement("a"); a.href = ${JSON.stringify(context.fixtures.url("pane-download.txt?download=1"))}; a.download = "rich-session-download.txt"; a.click(); })()`,
	);

	await app.waitFor("the download", (state) =>
		state.downloads.some((download) => download.state === "complete"),
	);

	const urls = [url, `${origin}/rich-session/script.js?value=${values.script}`];

	return {
		app,
		profile,
		tab,
		page,
		url,
		launch: folders.launch,
		planted: { values, hosts: [host], origins: [origin], urls },
		roots: {
			dataRoot: folders.dataDir,
			home: folders.home,
			temporary: [folders.temporary],
		},
	};
}

// Long enough that every store has reached the disk: History and Preferences
// commit within 10 s, and the cookie store within 30.
export const STORES_COMMITTED_MS = 35_000;

// Any of the planted values in the Mac's own temporary folder, the scenarios'
// folders aside: a helper can take it from macOS rather than from $TMPDIR. Only
// a planted value is looked for: the rest there is other apps'.
export async function plantedInRealTemporaryFolder(
	planted: Planted,
): Promise<string[]> {
	const patterns = join(tmpdir(), `pane-audit-${crypto.randomUUID()}.txt`);
	writeFileSync(patterns, Object.values(planted.values).join("\n"));
	const root = realpathSync(tmpdir());

	const found =
		await Bun.$`/usr/bin/grep -rlaFf ${patterns} --exclude-dir=pane-* --exclude=pane-audit-* ${root}`
			.quiet()
			.nothrow()
			.text();

	rmSync(patterns, { force: true });

	return found.split("\n").filter(Boolean);
}
