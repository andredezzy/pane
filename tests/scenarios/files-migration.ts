import { createHash } from "node:crypto";
import {
	existsSync,
	mkdirSync,
	readdirSync,
	readFileSync,
	rmSync,
	statSync,
	truncateSync,
	writeFileSync,
} from "node:fs";
import { dirname, join } from "node:path";
import { mainWindowOf } from "../lib/cua";
import { volumePassphrase } from "../lib/engine-key";
import { workerPage } from "../lib/extensions";
import { audit } from "../lib/file-audit";
import { launchPane, type PaneApp, type PaneLaunch, tabOf } from "../lib/pane";
import { waitForExit } from "../lib/processes";
import {
	READ_BACK,
	type ReadBack,
	type RichSession,
	richSession,
	STORES_COMMITTED_MS,
} from "../lib/rich-session";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario, type ScenarioContext } from "../run";

// A profile an earlier Pane wrote, opened with this one (ADR 0121): everything it
// held is still there, signed in, and nothing is left plain. Killed at points
// through the migration, it goes on and loses nothing. Started again on the
// migrated data, the earlier Pane opens nothing and changes nothing of it, and
// says the data won't open (Paper 07.8).
//
// Its safety copy comes back only after opens a kill ended before any came up:
// never after quits inside the first seconds, which keep what was written since.
// One that won't restore leaves the profile refused, and tried once.
//
// A profile with no folder yet, made without a tab, migrates nothing: its missing
// path resolved against the working directory, where the engine deleted a `Cache`
// and looked through a `Profile`.
//
// PANE_EARLIER_BUILDS names the published Panes' executables, comma-separated:
// each is the source of a pass.

const EXTENSION_NAME = "Pane rich session probe";

// Pane's own files that hold the user's data, hashed.
function ownFiles(dataDir: string): Record<string, string> {
	const hashes: Record<string, string> = {};

	for (const file of ["security.json", "Data.security.json", "profiles.json"]) {
		const path = join(dataDir, file);

		if (existsSync(path)) {
			hashes[file] = createHash("sha256")
				.update(readFileSync(path))
				.digest("hex");
		}
	}

	return hashes;
}

// Every file inside the volume, by hash and time, read by attaching it apart from
// any Pane, as only the data key allows. The image's own bands change whenever it
// attaches, as APFS writes its checkpoints; what the volume holds is what counts.
async function volumeFiles(
	context: ScenarioContext,
	dataDir: string,
): Promise<Record<string, string>> {
	const image = join(dataDir, "Data.sparsebundle");
	const mount = context.tempDir("pane-migration-mount-");

	const attached =
		await Bun.$`/usr/bin/hdiutil attach ${image} -nomount -plist -noverify -noautoopen`.text();

	const devices = [
		...attached.matchAll(/<string>(\/dev\/disk[0-9s]+)<\/string>/g),
	].map((match) => match[1] as string);

	const whole = devices.find((device) => /^\/dev\/disk\d+$/.test(device));
	const volume = devices.findLast((device) => /s\d+$/.test(device));
	expect(whole && volume, `no volume in the image: ${attached.slice(0, 300)}`);

	try {
		const unlock = Bun.spawn(
			[
				"/usr/sbin/diskutil",
				"apfs",
				"unlockVolume",
				volume as string,
				"-stdinpassphrase",
				"-mountpoint",
				mount,
			],
			{ stdin: "pipe", stdout: "ignore", stderr: "ignore" },
		);

		unlock.stdin.write(volumePassphrase(dataDir, null));
		await unlock.stdin.end();
		expect((await unlock.exited) === 0, "the volume would not unlock");

		const hashes: Record<string, string> = {};

		const visit = (folder: string) => {
			for (const entry of readdirSync(folder, { withFileTypes: true })) {
				const path = join(folder, entry.name);

				if (entry.name.startsWith(".fseventsd")) {
					continue;
				}

				if (entry.isDirectory()) {
					visit(path);
				} else if (entry.isFile()) {
					hashes[path.slice(mount.length)] = `${createHash("sha256")
						.update(readFileSync(path))
						.digest("hex")} ${statSync(path).mtimeMs}`;
				}
			}
		};

		visit(mount);

		return hashes;
	} finally {
		await Bun.$`/usr/bin/hdiutil detach ${whole as string} -force`
			.quiet()
			.nothrow();
	}
}

async function everything(
	app: PaneApp,
	session: RichSession,
): Promise<{ back: ReadBack; history: boolean; extension: unknown }> {
	const state = await app.waitFor(
		"the session's tab back",
		(current) => tabOf(current, session.tab)?.url === session.url,
		60_000,
	);

	expect(tabOf(state, session.tab), "the tab did not come back");
	await app.send("select", { tab: session.tab });
	const page = await app.page(session.tab, session.url);
	const back = await page.evaluate<ReadBack>(READ_BACK);

	const loaded = await app.waitFor("the extension", (current) =>
		(current.extensions[session.profile] ?? []).some(
			(item) => item.name === EXTENSION_NAME,
		),
	);

	const id = (loaded.extensions[session.profile] ?? []).find(
		(item) => item.name === EXTENSION_NAME,
	)?.id as string;

	const worker = await workerPage(app, id, session.url);

	const extension = await worker.evaluate<{ rich?: string }>(
		`chrome.storage.local.get("rich")`,
	);

	const found = await worker.evaluate<{ url: string }[]>(
		`chrome.history.search({ text: ${JSON.stringify(session.planted.values.query)}, startTime: 0, maxResults: 5 })`,
	);

	return { back, history: found.length > 0, extension: extension.rich };
}

function expectEverything(
	got: Awaited<ReturnType<typeof everything>>,
	session: RichSession,
	when: string,
) {
	const { values } = session.planted;
	const { back } = got;
	const say = `${when}: ${JSON.stringify(got)}`;

	expect(back.localStorage === values.localStorage, `local storage ${say}`);
	expect(back.indexedDB.value === values.indexedDB, `IndexedDB ${say}`);

	expect(
		back.indexedDB.blob.startsWith(values.indexedDBBlob ?? "") &&
			back.indexedDB.blobSize > 256 * 1024,
		`a blob ${say}`,
	);

	expect(back.cacheStorage === values.cacheStorage, `CacheStorage ${say}`);
	expect(back.originPrivate === values.originPrivate, `the OPFS ${say}`);

	expect(
		back.originPrivateSync === values.originPrivateSync,
		`a sync OPFS ${say}`,
	);

	expect(back.serviceWorker !== null, `the service worker ${say}`);
	expect(back.cookie.includes(values.scriptCookie ?? ""), `a cookie ${say}`);
	expect(got.history, `History ${say}`);

	expect(
		got.extension === values.extensionStorage,
		`an extension's data ${say}`,
	);
}

const basenameOf = (executable: string) =>
	executable.replace(/[^a-zA-Z0-9.]+/g, "-").slice(-40);

async function pass(context: ScenarioContext, earlier: string) {
	const session = await richSession(context, earlier);
	await Bun.sleep(STORES_COMMITTED_MS);
	await session.app.browser.close();

	const launch: PaneLaunch = { ...session.launch, binary: undefined };

	const dataDir = session.roots.dataRoot;

	const volume = join(dataDir, "Engine", "Volume");

	// Killed at points through its first open: at each, the next launch goes on.
	const killedAt: number[] = [];

	for (const after of [150, 400, 800]) {
		const app = await launchPane(context, launch);
		await app.send("select", { tab: session.tab }).catch(() => {});
		await Bun.sleep(after);

		// It may have crashed: that is what this looks for, past here.
		expect(
			app.browser.exit().code === null && app.browser.exit().signal === null,
			`Pane stopped on its own in its first opens: ${JSON.stringify(app.browser.exit())}`,
		);

		process.kill(app.browser.pid, "SIGKILL");
		await Bun.sleep(1_500);
		killedAt.push(after);
	}

	const app = await launchPane(context, launch);
	const got = await everything(app, session);
	expectEverything(got, session, "after the migration");

	await Bun.sleep(STORES_COMMITTED_MS);
	const found = audit(session.roots, session.planted);

	expect(
		found.plain.length === 0,
		`plain after the migration: ${found.plain.map((file) => `${file.path} (${file.plain.join(", ")})`).join("; ")}`,
	);

	expect(
		found.planted.length === 0,
		`planted values after the migration: ${found.planted.map((file) => `${file.path} (${file.planted.join(", ")})`).join("; ")}`,
	);

	expect(
		found.named.length === 0,
		`names after the migration: ${found.named.map((item) => `${item.path} (${item.named.join(", ")})`).join("; ")}`,
	);

	const safetyCopy = existsSync(join(volume, session.profile, "SafetyCopy"));
	expect(safetyCopy, "no safety copy");
	await app.browser.close();

	// The earlier Pane, on the migrated data, opens nothing and changes nothing.
	await Bun.sleep(2_000);
	const filesBefore = await volumeFiles(context, dataDir);
	const ownBefore = ownFiles(dataDir);
	expect(Object.keys(filesBefore).length > 100, "the volume looks empty");

	const fence = await launchPane(context, {
		...session.launch,
		volumeFails: true,
	});

	const refused = await fence.waitFor(
		"the earlier Pane's failure",
		(state) => state.security.data.failure !== null,
		30_000,
	);

	// What it shows, on screen, in each theme: Paper 07.8's lines.
	const name = basenameOf(earlier);
	const window = await mainWindowOf(fence.browser.pid);

	for (const theme of ["light", "dark"]) {
		await fence.send("theme", { theme });
		await Bun.sleep(500);

		await Bun.$`/usr/sbin/screencapture -x -o -l ${window} ${join(context.out, `fence-${name}-${theme}.png`)}`
			.quiet()
			.nothrow();
	}

	await fence.browser.close();
	await Bun.sleep(1_000);

	const filesAfter = await volumeFiles(context, dataDir);
	const ownAfter = ownFiles(dataDir);

	const changed = [
		...Object.keys({ ...filesBefore, ...filesAfter }).filter(
			(file) => filesBefore[file] !== filesAfter[file],
		),
		...Object.keys({ ...ownBefore, ...ownAfter }).filter(
			(file) => ownBefore[file] !== ownAfter[file],
		),
	];

	expect(
		changed.length === 0,
		`the earlier Pane changed ${changed.join(", ")}`,
	);

	// And this Pane still opens it all, after the earlier one's try.
	const again = await launchPane(context, launch);
	const gotAgain = await everything(again, session);
	expectEverything(gotAgain, session, "after the earlier Pane's try");
	await again.browser.close();

	return {
		earlier,
		killedAt,
		files: found.files.length,
		bytes: found.files.reduce((sum, file) => sum + file.size, 0),
		fence: refused.security.data.failure?.slice(0, 200),
		volumeFilesChecked: Object.keys(filesBefore).length,
	};
}

// A tabless profile's launch, from a folder that holds what the engine would
// have taken for the profile's own.
async function noFolder(context: ScenarioContext) {
	const dataDir = context.tempDir("pane-migration-tabless-");
	const first = await launchPane(context, { dataDir });
	await first.createProfile("No tab", "BLUE");
	await first.browser.close();

	const cwd = context.tempDir("pane-migration-cwd-");
	const keeps = ["Cache/keep", "Profile/keep"].map((file) => join(cwd, file));

	for (const keep of keeps) {
		mkdirSync(dirname(keep), { recursive: true });
		writeFileSync(keep, "kept");
	}

	const { app, trace } = await launchTraced(
		context,
		"files-migration-tabless",
		{
			dataDir,
			cwd,
		},
	);

	// Past the attach's migrations of every profile.
	await Bun.sleep(3_000);
	await app.browser.close();

	expect(
		keeps.every((keep) => existsSync(keep)),
		`a profile with no folder took from the working directory: ${keeps.filter((keep) => !existsSync(keep)).join(", ")}`,
	);

	expect(
		!trace().some(
			(line) => line.name === "PaneBrowserApplication.migrateContext",
		),
		"a profile with no folder migrated",
	);
}

// Opens of a migrated profile, each ended inside its first seconds.
async function quickOpen(
	context: ScenarioContext,
	session: RichSession,
	launch: PaneLaunch,
	end: (app: PaneApp) => Promise<void>,
	write?: string,
): Promise<{ read: string | null; ms: number }> {
	const started = Date.now();
	const app = await launchPane(context, launch);
	await app.send("select", { tab: session.tab });
	const page = await app.page(session.tab, session.url);

	if (write) {
		await page.evaluate(
			`localStorage.setItem("after", ${JSON.stringify(write)})`,
		);
	}

	const read = await page.evaluate<string | null>(
		`localStorage.getItem("after")`,
	);

	const ms = Date.now() - started;
	await end(app);
	// A kill's detach, by the watchdog, before the next attach.
	await Bun.sleep(1_000);

	return { read, ms };
}

const kill = async (app: PaneApp) => {
	process.kill(app.browser.pid, "SIGKILL");
	await waitForExit(app.browser.pid);
};

const quit = async (app: PaneApp) => {
	await app.browser.close();
};

// Quits inside the first seconds keep what was written since; kills before any
// open came up go back to the copy; a copy that won't restore is tried once.
async function safetyCopy(context: ScenarioContext, earlier: string) {
	const written = `after-${crypto.randomUUID()}`;
	const quits = await richSession(context, earlier);
	await quits.app.browser.close();
	const quitsLaunch: PaneLaunch = { ...quits.launch, binary: undefined };

	await quickOpen(context, quits, quitsLaunch, quit, written);

	for (let open = 1; open <= 4; open++) {
		const { read, ms } = await quickOpen(context, quits, quitsLaunch, quit);

		expect(
			read === written,
			`quit inside an open (${open}, ${ms} ms): what was written since the migration went: ${read}`,
		);
	}

	// Kills: the copy comes back, the session as it was, the later write gone.
	const kills = await richSession(context, earlier);
	await kills.app.browser.close();
	const killsLaunch: PaneLaunch = { ...kills.launch, binary: undefined };

	const killsFolder = join(
		kills.roots.dataRoot,
		"Engine",
		"Volume",
		kills.profile,
	);

	await quickOpen(context, kills, killsLaunch, kill, written);
	await quickOpen(context, kills, killsLaunch, kill);

	const restored = await launchPane(context, killsLaunch);
	await restored.send("select", { tab: kills.tab });
	const page = await restored.page(kills.tab, kills.url);
	const back = await page.evaluate<ReadBack>(READ_BACK);

	const afterRestore = await page.evaluate<string | null>(
		`localStorage.getItem("after")`,
	);

	expect(
		back.localStorage === kills.planted.values.localStorage &&
			afterRestore === null &&
			existsSync(join(killsFolder, ".safety-restored")),
		`kills before any open came up did not restore the copy: ${JSON.stringify({ back, afterRestore })}`,
	);

	await restored.browser.close();

	// A restore and a migration never share the folder: the copy marked for a
	// restore, the files marked not yet encrypted, as a kill in a migration
	// leaves them.
	const shared = await richSession(context, earlier);
	await shared.app.browser.close();
	const sharedLaunch: PaneLaunch = { ...shared.launch, binary: undefined };

	const sharedFolder = join(
		shared.roots.dataRoot,
		"Engine",
		"Volume",
		shared.profile,
	);

	await quickOpen(context, shared, sharedLaunch, kill);

	// A plain file big enough that the migration is still going at the select.
	await quickOpen(context, shared, sharedLaunch, async (app) => {
		rmSync(join(sharedFolder, ".files-encrypted"), { force: true });

		writeFileSync(
			join(sharedFolder, "Profile", "filler"),
			Buffer.alloc(600 * 1024 * 1024),
		);

		await kill(app);
	});

	const both = await launchTraced(
		context,
		"files-migration-shared",
		sharedLaunch,
	);

	await both.app.send("select", { tab: shared.tab });
	await both.app.page(shared.tab, shared.url);

	const order = both.trace().map((line) => line.name);
	const restoreAt = order.indexOf("PaneBrowserApplication.restoreContext");
	const migrateAt = order.indexOf("PaneBrowserApplication.migrateContext");

	const migratedAt = order.indexOf(
		"PaneBrowserApplication.migrateContext.completion",
	);

	expect(
		restoreAt < 0 ||
			migrateAt < 0 ||
			migrateAt > restoreAt ||
			(migratedAt >= 0 && migratedAt < restoreAt),
		`a restore and a migration on one folder: ${order.filter((name) => /migrateContext|restoreContext/.test(name)).join(", ")}`,
	);

	await both.app.browser.close();

	// A copy that won't restore: refused, tried once, and nothing else lost.
	const bad = await richSession(context, earlier);
	await bad.app.browser.close();
	const badLaunch: PaneLaunch = { ...bad.launch, binary: undefined };
	const badFolder = join(bad.roots.dataRoot, "Engine", "Volume", bad.profile);

	await quickOpen(context, bad, badLaunch, async (app) => {
		truncateSync(join(badFolder, "SafetyCopy"), 8);
		await kill(app);
	});

	await quickOpen(context, bad, badLaunch, kill);

	const refused = await launchTraced(
		context,
		"files-migration-bad-copy",
		badLaunch,
	);

	await refused.app.send("select", { tab: bad.tab });

	await refused.app.waitFor(
		"the sad tab of a profile whose copy won't restore",
		(state) => state.crashedTabs.includes(bad.tab),
		10_000,
	);

	// Long enough for a loop to show itself.
	await Bun.sleep(2_000);

	const tries = refused
		.trace()
		.filter(
			(line) => line.name === "PaneBrowserApplication.restoreContext",
		).length;

	expect(tries === 1, `a copy that won't restore was tried ${tries} times`);
	await refused.app.browser.close();
}

const filesMigration: Scenario = async (context) => {
	await noFolder(context);

	const builds = (process.env.PANE_EARLIER_BUILDS ?? "")
		.split(",")
		.filter(Boolean);

	if (builds.length === 0) {
		context.defer("no earlier build named (PANE_EARLIER_BUILDS)");

		return {};
	}

	const passes = [];

	for (const build of builds) {
		passes.push(await pass(context, build));
		await safetyCopy(context, build);
	}

	return { passes };
};

export default filesMigration;
