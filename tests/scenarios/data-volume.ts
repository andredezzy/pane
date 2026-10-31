import { existsSync, mkdirSync, rmSync } from "node:fs";
import { join } from "node:path";
import { launchPane, type PaneApp, tabOf } from "../lib/pane";
import { imageDevice, imagesAttachedIn } from "../lib/processes";
import { expect, type Scenario } from "../run";

// The image Pane finds at launch, and which Pane holds it (ADR 0113). A
// second Pane started on the same data, as from a terminal, hands over to the
// first through the engine's singleton and quits, and the first keeps its
// volume: the second had taken the first's image and detached it as it
// quit. An image whose making a quit cut short, before its APFS container or
// before its volume, is finished. A plain volume put in the encrypted one's
// place, which unlocks with nothing, is refused before anything is written
// to it. A quit while the first launch makes the image leaves nothing
// attached.
const dataVolume: Scenario = async (context) => {
	const dataDir = context.tempDir("pane-volume-");
	const image = join(dataDir, "Data.sparsebundle");
	const mountPoint = join(dataDir, "Engine", "Volume");

	const mounted = async () => {
		const table = await Bun.$`/sbin/mount`.text();

		return (
			table.includes(` on ${mountPoint} `) ||
			table.includes(` on /private${mountPoint} `)
		);
	};

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
				PANE_ELECTRON_DATA_DIR: join(dataDir, "no-electron-app"),
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
	};

	await first.browser.close();

	expect(
		handOver.before && handOver.exited && handOver.mounted && handOver.loads,
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
		await Bun.sleep(600);

		await plainApp.send("render", {
			path: join(context.out, `data-volume-07.8-${theme}.png`),
		});
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
			PANE_ELECTRON_DATA_DIR: join(quitDir, "no-electron-app"),
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

	return { handOver, finished, plain, quitWhileMaking };
};

export default dataVolume;
