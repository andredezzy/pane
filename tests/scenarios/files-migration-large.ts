import { join } from "node:path";
import { homeEnvironment } from "../lib/browser";
import { mainWindowOf } from "../lib/cua";
import { earlierBuilds } from "../lib/earlier-builds";
import { engineKey } from "../lib/engine-key";
import { workerPage } from "../lib/extensions";
import { launchPane, type PaneLaunch, tabOf } from "../lib/pane";
import type { NotOnWindows } from "../lib/platform";
import { READ_BACK, type ReadBack, richSession } from "../lib/rich-session";
import { captureWindow } from "../lib/tools";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// Where it runs (lib/platform.ts).
export const notOnWindows: NotOnWindows = {
	macOnly:
		"every row of ADR 0122's table is an earlier Pane's data on the Mac: here a large profile one before 0.18.0 wrote; no Pane was ever published for Windows, so every profile there starts encrypted (W2)",
};

// A large profile an earlier Pane wrote (PANE_EARLIER_BUILDS, the first whose
// files this one converts: before 0.18.0), opened with this one: thousands of
// history entries, many cookies, hundreds of MB of IndexedDB. It shows the move's
// panel past a second (Paper 07.5), goes on after being killed at random points,
// and everything is still there. Reports the migration's rate in MB/s.
const BLOB_BATCHES = Number(process.env.PANE_LARGE_BATCHES ?? 30);
const BLOB_MB_PER_BATCH = 10;
const HISTORY_ENTRIES = 10_000;
const COOKIES = 3_000;
const KILLS = 6;

const filesMigrationLarge: Scenario = async (context) => {
	const earlier = earlierBuilds().find((build) => !build.encrypts)?.executable;

	if (!earlier) {
		context.defer(
			"no earlier build whose files convert named (PANE_EARLIER_BUILDS)",
		);

		return {};
	}

	const session = await richSession(context, earlier);
	const { app, page, profile, url } = session;

	// Blobs, each of a size past what IndexedDB keeps in its LevelDB.
	await page.evaluate(`window.fillBlobs = (first, count) => new Promise((resolve, reject) => {
		const opened = indexedDB.open("rich-session", 1);
		opened.onerror = () => reject(opened.error);
		opened.onsuccess = () => {
			const tx = opened.result.transaction("items", "readwrite");
			const items = tx.objectStore("items");
			for (let i = 0; i < count; i++) {
				const bytes = new Uint8Array(1024 * 1024);
				for (let at = 0; at < bytes.length; at += 65536) {
					crypto.getRandomValues(bytes.subarray(at, Math.min(at + 65536, bytes.length)));
				}
				items.put(new Blob([bytes]), "large-" + (first + i));
			}
			tx.oncomplete = () => { opened.result.close(); resolve(); };
			tx.onerror = () => reject(tx.error);
		};
	})`);

	for (let batch = 0; batch < BLOB_BATCHES; batch++) {
		await page.evaluate(
			`fillBlobs(${batch * BLOB_MB_PER_BATCH}, ${BLOB_MB_PER_BATCH})`,
		);
	}

	const extension = (await app.state()).extensions[profile]?.find(
		(item) => item.name === "Pane rich session probe",
	)?.id as string;

	const worker = await workerPage(app, extension, url);

	await worker.evaluate(`(async () => {
		for (let start = 0; start < ${HISTORY_ENTRIES}; start += 200) {
			await Promise.all(Array.from({ length: 200 }, (_, i) =>
				chrome.history.addUrl({ url: "https://history-" + (start + i) + ".example/page/" + (start + i) })));
		}
	})()`);

	await worker.evaluate(`(async () => {
		for (let start = 0; start < ${COOKIES}; start += 200) {
			await Promise.all(Array.from({ length: 200 }, (_, i) =>
				chrome.cookies.set({ url: ${JSON.stringify(`${new URL(url).origin}/`)}, name: "c" + (start + i), value: "v".repeat(64), expirationDate: Date.now() / 1000 + 86400 })));
		}
	})()`);

	await Bun.sleep(35_000);
	await app.browser.close();

	const launch: PaneLaunch = { ...session.launch, binary: undefined };
	const dataDir = session.roots.dataRoot;
	const engineKeyBefore = engineKey(dataDir, profile, null);
	expect(engineKeyBefore.length === 32, "no key");

	const volumeBytes =
		Number(
			(await Bun.$`du -sk ${dataDir}/Data.sparsebundle`.text()).split(/\s+/)[0],
		) * 1024;

	// A copy for the kills, since the timed run below finishes the migration.
	const killedHome = context.tempDir("pane-large-killed-");
	await Bun.$`cp -R ${session.roots.home}/ ${killedHome}/`.quiet();

	const killedLaunch: PaneLaunch = {
		dataDir: join(
			killedHome,
			"Library/Application Support/xyz.roundedcorners.pane",
		),
		derivedDataRoot: true,
		env: {
			...homeEnvironment(killedHome),
			TMPDIR: `${join(killedHome, "tmp")}/`,
		},
		args: launch.args,
	};

	// The migration, timed, the panel seen, from the trace.
	const { app: finishing, trace } = await launchTraced(
		context,
		"files-migration-large",
		launch,
	);

	await finishing.send("select", { tab: session.tab }).catch(() => {});
	let sawPanel = false;
	let share = -1;

	for (const until = Date.now() + 300_000; Date.now() < until; ) {
		const state = await finishing.state();
		const moving = /^moving (.+)/.exec(state.security.data.phase);

		if (moving) {
			// Paper 07.5's panel, on screen, once.
			if (!sawPanel) {
				await captureWindow(
					await mainWindowOf(finishing.browser.pid),
					join(context.out, "migration-panel.png"),
				);
			}

			sawPanel = true;
			share = Number(moving[1]);
		}

		if (
			trace().some(
				(line) =>
					line.name === "PaneBrowserApplication.migrateContext.completion",
			) &&
			tabOf(state, session.tab)?.loading === false &&
			state.security.data.phase === "open"
		) {
			break;
		}

		await Bun.sleep(50);
	}

	const lines = trace();

	const started = lines.find(
		(line) => line.name === "PaneBrowserApplication.migrateContext",
	);

	const ended = lines.find(
		(line) => line.name === "PaneBrowserApplication.migrateContext.completion",
	);

	expect(started && ended, "the migration's trace is not whole");
	expect(ended?.args.migrated === true, "the migration failed");
	const milliseconds = (ended?.t ?? 0) - (started?.t ?? 0);

	const tabPage = await finishing.page(session.tab, url);
	const back = await tabPage.evaluate<ReadBack>(READ_BACK);
	const { values } = session.planted;

	expect(
		back.localStorage === values.localStorage &&
			back.indexedDB.value === values.indexedDB &&
			back.cacheStorage === values.cacheStorage &&
			back.originPrivate === values.originPrivate,
		`after the large migration: ${JSON.stringify(back)}`,
	);

	const large = await tabPage.evaluate<{
		size: number;
		firstBytesVary: boolean;
	}>(`new Promise((resolve) => {
		const opened = indexedDB.open("rich-session", 1);
		opened.onsuccess = () => {
			const get = opened.result.transaction("items").objectStore("items").get("large-${BLOB_BATCHES * BLOB_MB_PER_BATCH - 1}");
			get.onsuccess = async () => {
				const bytes = new Uint8Array(await get.result.arrayBuffer());
				resolve({ size: bytes.length, firstBytesVary: new Set(bytes.subarray(0, 64)).size > 16 });
			};
		};
	})`);

	expect(
		large.size === 1024 * 1024 && large.firstBytesVary,
		`the last large blob: ${JSON.stringify(large)}`,
	);

	const worker2 = await workerPage(finishing, extension, url);

	const history = await worker2.evaluate<number>(
		`chrome.history.search({ text: "history-${HISTORY_ENTRIES - 1}", startTime: 0, maxResults: 3 }).then((found) => found.length)`,
	);

	expect(history > 0, "the last history entry is gone");
	await finishing.browser.close();

	// Killed at random points of as long as it takes: at each it goes on.
	const kills: number[] = [];

	for (let kill = 0; kill < KILLS; kill++) {
		const running = await launchPane(context, killedLaunch);
		await running.send("select", { tab: session.tab }).catch(() => {});
		const after = Math.floor(Math.random() * milliseconds);
		await Bun.sleep(after);

		expect(
			running.browser.exit().code === null &&
				running.browser.exit().signal === null,
			`it stopped on its own: ${JSON.stringify(running.browser.exit())}`,
		);

		process.kill(running.browser.pid, "SIGKILL");
		kills.push(after);
		await Bun.sleep(1_500);
	}

	const again = await launchPane(context, killedLaunch);
	await again.send("select", { tab: session.tab }).catch(() => {});

	const backAgain = await (
		await again.page(session.tab, url)
	).evaluate<ReadBack>(READ_BACK);

	expect(
		backAgain.localStorage === values.localStorage &&
			backAgain.indexedDB.value === values.indexedDB &&
			backAgain.cacheStorage === values.cacheStorage &&
			backAgain.originPrivate === values.originPrivate,
		`after the kills: ${JSON.stringify(backAgain)}`,
	);

	await again.browser.close();

	return {
		volumeBytes,
		kills,
		milliseconds,
		sawPanel,
		lastShare: share,
		megabytesPerSecond: Math.round(volumeBytes / 1e3 / milliseconds),
	};
};

export default filesMigrationLarge;
