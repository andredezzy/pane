import { createHash } from "node:crypto";
import {
	existsSync,
	mkdirSync,
	readdirSync,
	readFileSync,
	rmSync,
	statSync,
} from "node:fs";
import { join } from "node:path";
import { formerVolumePassphrase } from "../lib/engine-key";
import { launchPane, type PaneApp, tabOf } from "../lib/pane";
import {
	imageDevice,
	imagesAttachedIn,
	mountedAt,
	pgrep,
} from "../lib/processes";
import {
	READ_BACK,
	type ReadBack,
	type RichSession,
	richSession,
	STORES_COMMITTED_MS,
} from "../lib/rich-session";
import { expect, type Scenario, type ScenarioContext } from "../run";

// The image Pane finds at launch, and which Pane holds it (ADR 0113). A
// second Pane started on the same data, as from a terminal, hands over to the
// first through the engine's singleton and quits, and the first keeps its
// volume: the second had taken the first's image and detached it as it
// quit. An image whose making a quit cut short, before its APFS container or
// before its volume, is finished. A plain volume put in the encrypted one's
// place, which unlocks with nothing, is refused before anything is written
// to it. A quit while the first launch makes the image leaves nothing
// attached. A data folder from before the floor (ADR 0122) is refused, with every
// hash and time unchanged, and the Pane that wrote it still opens it all:
// PANE_EARLIER_BUILDS names that Pane's executables, comma-separated. With
// PANE_FLOOR_BUILD naming the floor's, what it converted opens here, and what its
// conversion left goes.

// A file's hash and time.
function hashAndTime(path: string) {
	return `${createHash("sha256").update(readFileSync(path)).digest("hex")} ${statSync(path).mtimeMs}`;
}

// Every file inside the volume, by hash and time, read by attaching it apart from
// any Pane, as only the data key allows. The image's own bands change whenever it
// attaches, as APFS writes its checkpoints; what the volume holds is what counts.
async function volumeFiles(
	context: ScenarioContext,
	dataDir: string,
): Promise<Record<string, string>> {
	const image = join(dataDir, "Data.sparsebundle");
	const mount = context.tempDir("pane-volume-look-");

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

		unlock.stdin.write(formerVolumePassphrase(dataDir, null));
		await unlock.stdin.end();
		expect((await unlock.exited) === 0, "the volume would not unlock");

		const files: Record<string, string> = {};

		const visit = (folder: string) => {
			for (const entry of readdirSync(folder, { withFileTypes: true })) {
				const path = join(folder, entry.name);

				if (/^\.(fseventsd|Spotlight-V100)/.test(entry.name)) {
					continue;
				}

				if (entry.isDirectory()) {
					visit(path);
				} else if (entry.isFile()) {
					files[path.slice(mount.length)] = hashAndTime(path);
				}
			}
		};

		visit(mount);

		return files;
	} finally {
		await Bun.$`/usr/bin/hdiutil detach ${whole as string} -force`
			.quiet()
			.nothrow();
	}
}

// Pane's own files that hold the user's data.
function ownFiles(dataDir: string): Record<string, string> {
	const files: Record<string, string> = {};

	for (const file of [
		"security.json",
		"Data.security.json",
		"profiles.json",
		"profiles.json.damaged",
		"security.json.damaged",
		"settings.json",
		"deletions.json",
	]) {
		const path = join(dataDir, file);

		if (existsSync(path)) {
			files[file] = hashAndTime(path);
		}
	}

	return files;
}

// A data folder an earlier Pane wrote, whose volume is on the passphrase ADR 0121
// moved away from.
async function beforeTheFloor(context: ScenarioContext, earlier: string) {
	const session = await richSession(context, earlier);
	await Bun.sleep(STORES_COMMITTED_MS);
	await session.app.browser.close();
	await Bun.sleep(2_000);

	const dataDir = session.roots.dataRoot;

	const before = {
		...(await volumeFiles(context, dataDir)),
		...ownFiles(dataDir),
	};

	expect(Object.keys(before).length > 100, "the volume looks empty");

	const refused = await launchPane(context, {
		...session.launch,
		binary: undefined,
		volumeFails: true,
	});

	const state = await refused.waitFor(
		"the lock saying the data won't open",
		(current) =>
			current.security.locked &&
			current.security.screen?.error != null &&
			current.security.data.failure !== null,
		30_000,
	);

	await refused.browser.close();
	await Bun.sleep(1_000);

	const after = {
		...(await volumeFiles(context, dataDir)),
		...ownFiles(dataDir),
	};

	const changed = Object.keys({ ...before, ...after }).filter(
		(file) => before[file] !== after[file],
	);

	expect(
		state.security.screen?.error ===
			"Pane can\u{2019}t open your encrypted data.\nQuit, then open Pane again.",
		`a data folder from before the floor: ${JSON.stringify(state.security)}`,
	);

	expect(changed.length === 0, `the refused launch changed ${changed}`);

	// The Pane that wrote it still opens it all: what the floor's dmg then converts.
	const again = await launchPane(context, session.launch);
	await expectSession(again, session, "after the refused launch");
	await again.browser.close();

	return {
		earlier,
		filesChecked: Object.keys(before).length,
		failure: state.security.data.failure?.slice(0, 200),
	};
}

// What the session planted, read back through its page.
async function expectSession(app: PaneApp, session: RichSession, when: string) {
	await app.send("select", { tab: session.tab });
	const page = await app.page(session.tab, session.url);
	const back = await page.evaluate<ReadBack>(READ_BACK);
	const { values } = session.planted;

	expect(
		back.localStorage === values.localStorage &&
			back.indexedDB.value === values.indexedDB &&
			back.cacheStorage === values.cacheStorage &&
			back.originPrivate === values.originPrivate &&
			back.cookie.includes(values.scriptCookie ?? ""),
		`what the session wrote, ${when}: ${JSON.stringify(back)}`,
	);
}

// The way a user comes: a folder from before the floor, converted by the floor's
// Pane, then opened by this one. Nothing changes under the floor, so its
// profiles.json is still as the earlier Pane wrote it, and must read here.
async function throughTheFloor(
	context: ScenarioContext,
	earlier: string,
	floor: string,
) {
	const session = await richSession(context, earlier);
	await Bun.sleep(STORES_COMMITTED_MS);
	await session.app.browser.close();

	const profile = join(
		session.roots.dataRoot,
		"Engine",
		"Volume",
		session.profile,
	);

	const left = () =>
		readdirSync(profile).filter((name) =>
			/^(SafetyCopy|\.safety-.*|\.files-repaired)$/.test(name),
		);

	const converting = await launchPane(context, {
		...session.launch,
		binary: floor,
	});

	await expectSession(converting, session, `on ${floor}`);
	const leftByTheFloor = left();
	await converting.browser.close();

	const app = await launchPane(context, {
		...session.launch,
		binary: undefined,
	});

	await expectSession(app, session, "past the floor");
	const leftHere = left();
	const marked = existsSync(join(profile, ".files-encrypted"));
	await app.browser.close();

	expect(
		leftByTheFloor.includes("SafetyCopy") && leftHere.length === 0 && marked,
		`what the conversion left: ${JSON.stringify({ leftByTheFloor, leftHere, marked })}`,
	);

	return { earlier, floor, leftByTheFloor };
}

const dataVolume: Scenario = async (context) => {
	const dataDir = context.tempDir("pane-volume-");
	const image = join(dataDir, "Data.sparsebundle");
	const mountPoint = join(dataDir, "Engine", "Volume");

	const mounted = () => mountedAt(mountPoint);

	// A new page in the profile loads, its data in the volume.
	const loads = async (app: PaneApp, profile: string, query: string) => {
		const url = context.fixtures.url(`nav-a.html?${query}`);
		const tab = await app.openTab(profile, url);

		return app
			.waitFor(
				`the page ${query}`,
				(state) =>
					tabOf(state, tab)?.url === url &&
					tabOf(state, tab)?.loading === false,
				10_000,
			)
			.then(
				() => true,
				() => false,
			);
	};

	// The image attached, unmounted or mounted read-only out of the Finder:
	// its own device.
	const attach = async (mount: string[]) => {
		const entities = (
			JSON.parse(
				await Bun.$`/usr/bin/hdiutil attach ${image} ${mount} -plist -noverify -noautoopen | /usr/bin/plutil -convert json -o - -`
					.quiet()
					.text(),
			) as {
				"system-entities": { "dev-entry"?: string; "content-hint"?: string }[];
			}
		)["system-entities"];

		const device = imageDevice(entities);

		if (!device) {
			throw new Error(`no device for ${image}`);
		}

		return device;
	};

	const first = await launchPane(context, { dataDir });
	const profile = await first.createProfile("Work", "BLUE");
	const before = await loads(first, profile, "before");

	const home = join(dataDir, "home");

	const second = Bun.spawn(
		[
			context.binary,
			"--no-first-run",
			"--no-default-browser-check",
			"--use-mock-keychain",
			context.fixtures.url("nav-a.html?second"),
		],
		{
			env: {
				...process.env,
				HOME: home,
				CFFIXED_USER_HOME: home,
				PANE_DATA_DIR: dataDir,
			},
			stdout: "ignore",
			stderr: "ignore",
		},
	);

	const exited = await Promise.race([
		second.exited.then(() => true),
		Bun.sleep(30_000).then(() => false),
	]);

	if (!exited) {
		second.kill("SIGKILL");
	}

	// Long enough for a watchdog the second started to have detached the
	// image, were there one.
	await Bun.sleep(1_000);

	const handOver = {
		before,
		exited,
		exitCode: second.exitCode,
		mounted: await mounted(),
		loads: await loads(first, profile, "after"),
		watchdogs: (await pgrep("-f", `pane-volume-watch ${first.browser.pid} `))
			.length,
	};

	await first.browser.close();

	expect(
		handOver.before &&
			handOver.exited &&
			handOver.mounted &&
			handOver.loads &&
			handOver.watchdogs === 1,
		`a second Pane on the same data: ${JSON.stringify(handOver)}`,
	);

	// A quit cut the image's making short: before its container, then
	// before its volume. The key was saved first, and Pane finishes it.
	const cutShort = async (stage: "container" | "volume") => {
		rmSync(image, { recursive: true, force: true });
		await Bun.$`/usr/bin/hdiutil create -size 1t -type SPARSEBUNDLE -layout NONE ${join(dataDir, "Data")} -quiet`;

		if (stage === "volume") {
			const device = await attach(["-nomount"]);
			await Bun.$`/sbin/newfs_apfs -C -w ${device}`.quiet();
			await Bun.$`/usr/bin/hdiutil detach ${device} -quiet`;
		}

		const app = await launchPane(context, { dataDir });
		const state = await app.state();

		const finished = {
			attached: state.security.data.attached,
			mounted: await mounted(),
			loads: await loads(app, profile, `before-its-${stage}`),
		};

		await app.browser.close();

		return finished;
	};

	const finished = {
		beforeItsContainer: await cutShort("container"),
		beforeItsVolume: await cutShort("volume"),
	};

	expect(
		Object.values(finished).every(
			(stage) => stage.attached && stage.mounted && stage.loads,
		),
		`an image whose making was cut short: ${JSON.stringify(finished)}`,
	);

	// A plain volume in the encrypted one's place.
	rmSync(image, { recursive: true, force: true });
	await Bun.$`/usr/bin/hdiutil create -size 1g -type SPARSEBUNDLE -fs APFS -volname Pane ${join(dataDir, "Data")} -quiet`;

	const plainApp = await launchPane(context, { dataDir, volumeFails: true });

	// The lock shuts and says so (07.8).
	const plainState = await plainApp.waitFor(
		"the lock saying the data won't open",
		(state) => state.security.locked && state.security.screen?.error != null,
	);

	const plain = {
		failure: plainState.security.data.failure,
		locked: plainState.security.locked,
		message: plainState.security.screen?.error,
		mounted: await mounted(),
		attachedImages: (await imagesAttachedIn(dataDir)).length,
		profileFolder: false,
	};

	// The panel as Paper's 07.8 draws it, in both themes, once it has slid
	// in.
	for (const theme of ["light", "dark"]) {
		await plainApp.send("theme", { theme });
		await plainApp.render(`data-volume-07.8-${theme}.png`, 600);
	}

	await plainApp.browser.close();

	// Nothing of the profile's reached it.
	const look = context.tempDir("pane-volume-plain-");
	mkdirSync(look, { recursive: true });
	const device = await attach(["-readonly", "-nobrowse", "-mountpoint", look]);

	try {
		plain.profileFolder = existsSync(join(look, profile));
	} finally {
		await Bun.$`/usr/bin/hdiutil detach ${device} -force -quiet`.nothrow();
	}

	expect(
		(plain.failure ?? "").includes("not encrypted") &&
			plain.message ===
				"Pane can\u{2019}t open your encrypted data.\nQuit, then open Pane again." &&
			!plain.mounted &&
			plain.attachedImages === 0 &&
			!plain.profileFolder,
		`a plain volume in the encrypted one's place: ${JSON.stringify(plain)}`,
	);

	// A quit while the first launch makes the image: Pane waits for the
	// attach under way, then detaches it. It had left the image attached
	// once the attach finished, after Pane and before its watchdog.
	const quitDir = context.tempDir("pane-volume-quit-");
	const quitHome = join(quitDir, "home");
	mkdirSync(quitHome, { recursive: true });

	const quitting = await context.launch({
		env: {
			PANE_DATA_DIR: quitDir,
			HOME: quitHome,
			CFFIXED_USER_HOME: quitHome,
		},
	});

	await Bun.sleep(700);
	await quitting.close();

	// Long enough for an attach left under way to have finished.
	await Bun.sleep(3_000);

	const quitWhileMaking = {
		made: existsSync(join(quitDir, "Data.sparsebundle")),
		attached: (await imagesAttachedIn(quitDir)).length,
	};

	expect(
		quitWhileMaking.attached === 0,
		`a quit while the image was made: ${JSON.stringify(quitWhileMaking)}`,
	);

	// An assistive app asks for the accessibility tree just before a quit. Chromium
	// answers 2 s later, on a timer that the wait for the volume's detach fired with
	// the engine gone: an abort at the quit, from 0.14 to 0.17. The quit takes about
	// a second, so these land on either side of the wait.
	const assisted: {
		waitedMs: number;
		code: number | null;
		signal: string | null;
	}[] = [];

	for (const waitedMs of [400, 900, 1_300, 1_700]) {
		const app = await launchPane(context);
		await app.send("requestEnhancedUserInterface");
		await Bun.sleep(waitedMs);
		await app.browser.close();
		assisted.push({ waitedMs, ...app.browser.exit() });
	}

	expect(
		assisted.every(({ code, signal }) => code === 0 && signal === null),
		`quits after an assistive app's request: ${JSON.stringify(assisted)}`,
	);

	const builds = (process.env.PANE_EARLIER_BUILDS ?? "")
		.split(",")
		.filter(Boolean);

	if (builds.length === 0) {
		context.defer("no earlier build named (PANE_EARLIER_BUILDS)");
	}

	const refused = [];

	for (const build of builds) {
		refused.push(await beforeTheFloor(context, build));
	}

	const floor = process.env.PANE_FLOOR_BUILD;
	const earlier = builds[0];

	if (floor && earlier) {
		await throughTheFloor(context, earlier, floor);
	} else {
		context.defer(
			"no floor to convert an earlier build's data (PANE_FLOOR_BUILD, PANE_EARLIER_BUILDS)",
		);
	}
};

export default dataVolume;
