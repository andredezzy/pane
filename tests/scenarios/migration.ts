import { createHash } from "node:crypto";
import {
	existsSync,
	lstatSync,
	mkdirSync,
	readdirSync,
	readFileSync,
	statSync,
	writeFileSync,
} from "node:fs";
import { join, relative } from "node:path";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// The first launch after the Electron app. Electron itself (41.4.0, the
// version the Electron app shipped, kept as a dev dependency for this) writes
// a profile's site data, a cookie and local storage, into a throwaway data
// directory beside the app's JSON files (two profiles, their tabs, settings,
// a PIN) and an installed extension. Pane starts while the Electron app
// still runs on it (its single-instance lock held): it waits, having written
// nothing, and goes on once the Electron app quits. It migrates everything,
// from a copy: it starts locked behind the same PIN, the profiles, tabs, and
// settings are there, and the profile's pages see the Electron app's cookie
// and storage. The extension, which the Electron app installed once for
// every profile, reinstalls in none: each profile adds its own. The Electron
// data the migration reads is byte for byte as it was.

const ELECTRON = join(
	import.meta.dir,
	"../../node_modules/electron/dist/Electron.app/Contents/MacOS/Electron",
);

const WORK = "6f9d2c1a-8b3e-4c7d-9a2f-1e5b7c3d9f01";
const PERSONAL = "0a7e4b2c-1d3f-4e5a-8b9c-2d4e6f8a0b13";
// uBlock Origin Lite, as the ubol scenario installs it.
const EXTENSION = "ddkjiahejlhfcafbddmgiahcphecmpfh";

// Electron's own session API, as the app used it: a cookie through the
// profile's partition, and local storage from a page of it.
const SEED = `
const { app, session, BrowserWindow } = require("electron");
app.setName("@pane/veil");
app.setPath("userData", process.env.SEED_DIR);
app.whenReady().then(async () => {
	const partition = "persist:profile-" + process.env.SEED_PROFILE;
	const profile = session.fromPartition(partition);
	await profile.cookies.set({ url: process.env.SEED_ORIGIN + "/", name: "pane_session", value: "electron-signed-in", expirationDate: Math.floor(Date.now() / 1000) + 86400 });
	await profile.cookies.flushStore();
	const window = new BrowserWindow({ show: false, webPreferences: { partition } });
	await window.loadURL(process.env.SEED_ORIGIN + "/nav-b.html?seed");
	await window.webContents.executeJavaScript('localStorage.setItem("pane-electron", "kept")');
	profile.flushStorageData();
	setTimeout(() => app.quit(), 1000);
});
`;

// The Electron app running on its data, as the user left it open: it holds
// its single-instance lock (SingletonLock) for HOLD_MS, then quits.
const HOLD = `
const { app } = require("electron");
app.setName("@pane/veil");
app.setPath("userData", process.env.SEED_DIR);
if (!app.requestSingleInstanceLock()) app.exit(1);
app.whenReady().then(() => setTimeout(() => app.quit(), Number(process.env.HOLD_MS)));
`;

// What the migration reads: the JSON files, the partitions, and the
// extensions. The running Electron app rewrites its own session files at
// the root (Local State, Preferences), as it always did.
function migratedData(files: Record<string, string>): Record<string, string> {
	return Object.fromEntries(
		Object.entries(files).filter(
			([path]) =>
				path.startsWith("Partitions/") ||
				path.startsWith("Extensions/") ||
				/^(profiles|settings|security)\.json$/.test(path),
		),
	);
}

function snapshot(root: string): Record<string, string> {
	const files: Record<string, string> = {};

	const walk = (directory: string) => {
		for (const name of readdirSync(directory)) {
			const path = join(directory, name);
			const stat = statSync(path, { throwIfNoEntry: false });

			if (stat?.isDirectory()) {
				walk(path);
			} else if (stat) {
				files[relative(root, path)] = createHash("sha256")
					.update(readFileSync(path))
					.digest("hex");
			}
		}
	};

	walk(root);

	return files;
}

const migration: Scenario = async (context) => {
	expect(existsSync(ELECTRON), `no Electron at ${ELECTRON}`);

	// The Electron app's data.
	const source = context.tempDir("pane-electron-");

	const scripts = context.tempDir("pane-electron-seed-");
	const seed = join(scripts, "main.js");
	const hold = join(scripts, "hold.js");

	writeFileSync(seed, SEED);
	writeFileSync(hold, HOLD);

	const seeding = Bun.spawn([ELECTRON, seed, "--use-mock-keychain"], {
		env: {
			...process.env,
			SEED_DIR: source,
			SEED_PROFILE: WORK,
			SEED_ORIGIN: context.fixtures.origin,
		},
		stdout: "ignore",
		stderr: "ignore",
	});

	expect((await seeding.exited) === 0, "Electron could not seed its data");

	const tab = (id: string, path: string, title: string) => ({
		id,
		url: context.fixtures.url(path),
		title,
		favicon: "",
		isLoaded: false,
	});

	const profile = (
		id: string,
		name: string,
		color: string,
		extra: Record<string, unknown>,
		tabs: unknown[],
	) => ({
		id,
		name,
		color,
		group: null,
		fingerprint: null,
		proxy: null,
		keepLoaded: false,
		tabs,
		createdAt: "2026-09-01T10:00:00.000Z",
		updatedAt: "2026-10-20T10:00:00.000Z",
		...extra,
	});

	const windows = {
		userAgent:
			"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/136.0.0.0 Safari/537.36",
		platform: "WINDOWS",
		screen: { width: 1920, height: 1080, colorDepth: 24 },
		language: "en-US",
		languages: ["en-US", "en"],
		timezone: "America/New_York",
		webgl: {
			vendor: "Google Inc. (NVIDIA)",
			renderer:
				"ANGLE (NVIDIA, NVIDIA GeForce RTX 3060 Direct3D11 vs_5_0 ps_5_0, D3D11)",
		},
		hardwareConcurrency: 8,
		deviceMemory: 8,
		maxTouchPoints: 0,
		canvas: { noise: true },
		audio: { noise: true },
	};

	const json = (state: unknown) => JSON.stringify({ state, version: 0 });

	writeFileSync(
		join(source, "profiles.json"),
		json({
			profiles: [
				profile(
					WORK,
					"Work",
					"ROSE",
					{ fingerprint: windows, keepLoaded: true },
					[
						tab("w1", "nav-a.html?migrated", "Navigation A"),
						tab("w2", "nav-b.html?migrated", "Navigation B"),
					],
				),
				profile(PERSONAL, "Personal", "EMERALD", { group: "Home" }, [
					tab("p1", "smoke.html?migrated", "Smoke"),
				]),
			],
		}),
	);

	writeFileSync(
		join(source, "settings.json"),
		json({
			settings: {
				chromiumPath: "",
				theme: "dark",
				tabSleepAfterMinutes: 5,
				profileUnloadAfterMinutes: null,
				cacheBudgetMB: 1024,
			},
		}),
	);

	// bcryptjs 3.0.3, hashSync("1234", 10).
	writeFileSync(
		join(source, "security.json"),
		json({
			pin: {
				hash: "$2b$10$6RNvGASNquZ6f5LXjFap1eMcHOZTOzRYJVRe1DrjRG2juL90W5MNO",
				length: 4,
			},
			failedAttempts: 0,
		}),
	);

	mkdirSync(join(source, "Extensions", EXTENSION, "2025.1.1_0"), {
		recursive: true,
	});

	writeFileSync(
		join(source, "Extensions", EXTENSION, "2025.1.1_0", "manifest.json"),
		"{}",
	);

	const before = snapshot(source);

	const partition = Object.keys(before).filter((path) =>
		path.startsWith(`Partitions/profile-${WORK}/`),
	);

	expect(
		partition.some((path) => path.endsWith("Cookies")),
		`Electron wrote no cookies: ${JSON.stringify(partition)}`,
	);

	// Pane's first launch, while the Electron app runs.
	const holding = Bun.spawn([ELECTRON, hold, "--use-mock-keychain"], {
		env: { ...process.env, SEED_DIR: source, HOLD_MS: "6000" },
		stdout: "ignore",
		stderr: "ignore",
	});

	// A link to "<host>-<pid>", which existsSync, following it, never finds.
	const locked = () =>
		!!lstatSync(join(source, "SingletonLock"), { throwIfNoEntry: false });

	const lockDeadline = Date.now() + 15_000;

	while (!locked() && Date.now() < lockDeadline) {
		await Bun.sleep(100);
	}

	expect(locked(), "the Electron app took no single-instance lock");

	const dataDir = context.tempDir("pane-data-");

	const launching = launchTraced(context, "migration", {
		dataDir,
		env: { PANE_ELECTRON_DATA_DIR: source },
	});

	await Bun.sleep(3_000);

	const waited = {
		electronRunning: holding.exitCode === null,
		wrote: [
			"profiles.json",
			"settings.json",
			"migration.json",
			"Engine",
		].filter((name) => existsSync(join(dataDir, name))),
	};

	const { app, has } = await launching;

	expect(
		waited.electronRunning && waited.wrote.length === 0,
		`Pane did not wait for the Electron app: ${JSON.stringify(waited)}`,
	);

	expect(holding.exitCode !== null, "Pane went on while the Electron app ran");

	const launched = await app.state();

	expect(
		launched.security.locked && launched.security.pinLength === 4,
		`not locked behind the migrated PIN: ${JSON.stringify(launched.security)}`,
	);

	await app.send("pinKeys", { keys: "1234" });

	// The PIN opens the data key; the Electron app's profiles then move into
	// the data volume (ADR 0113), and Pane opens.
	const unlocked = await app.waitFor(
		"unlocked, its data moved in",
		(state) => !state.security.locked && state.security.data.phase === "open",
		60_000,
	);

	const profiles = unlocked.profiles.map((entry) => ({
		id: entry.id,
		name: entry.name,
		color: entry.color,
		keepLoaded: entry.keepLoaded,
		tabs: entry.tabs.map((item) => item.url),
	}));

	expect(
		JSON.stringify(profiles.map((entry) => entry.name)) ===
			'["Work","Personal"]' &&
			profiles[0]?.color === "ROSE" &&
			profiles[0]?.keepLoaded === true &&
			profiles[0]?.tabs.length === 2 &&
			profiles[1]?.tabs[0] === context.fixtures.url("smoke.html?migrated"),
		`the migrated profiles: ${JSON.stringify(profiles)}`,
	);

	expect(unlocked.theme === "dark", `the migrated theme: ${unlocked.theme}`);

	// The Electron app's cookie and storage, in the profile's page.
	const url = context.fixtures.url("nav-a.html?migrated");
	await app.send("select", { tab: "w1" });
	const page = await app.page("w1", url);

	const seen = await page.evaluate<{
		cookie: string;
		storage: string | null;
		platform: string;
	}>(
		`({ cookie: document.cookie, storage: localStorage.getItem("pane-electron"), platform: navigator.platform })`,
	);

	expect(
		seen.cookie.includes("pane_session=electron-signed-in"),
		`the page's cookies: ${seen.cookie}`,
	);

	expect(seen.storage === "kept", `the page's local storage: ${seen.storage}`);

	expect(
		seen.platform === "Win32",
		`the migrated fingerprint: ${seen.platform}`,
	);

	// Both profiles open; neither reinstalls the Electron app's extension,
	// and migration.json lists none to reinstall.
	await app.send("select", { tab: "p1" });

	await app.waitFor(
		"both profiles open",
		(state) => WORK in state.extensions && PERSONAL in state.extensions,
	);

	// Long enough for a reinstall to start (it did as each profile opened).
	await Bun.sleep(5_000);
	const opened = await app.state();

	const record = JSON.parse(
		readFileSync(join(app.dataDir, "migration.json"), "utf8"),
	) as { state: Record<string, unknown> };

	await app.browser.close();
	const after = snapshot(source);

	expect(
		JSON.stringify(migratedData(after)) ===
			JSON.stringify(migratedData(before)),
		"the Electron data changed",
	);

	const extensions = {
		work: (opened.extensions[WORK] ?? []).map((entry) => entry.id),
		personal: (opened.extensions[PERSONAL] ?? []).map((entry) => entry.id),
	};

	expect(
		extensions.work.length === 0 &&
			extensions.personal.length === 0 &&
			!has("PaneBrowserContext.installExtension") &&
			!("pendingExtensions" in record.state),
		`the Electron app's extension reinstalled: ${JSON.stringify({ extensions, record: record.state })}`,
	);

	return {
		source: {
			files: Object.keys(before).length,
			partitionFiles: partition.length,
			unchanged: true,
		},
		whileElectronRan: waited,
		pin: {
			lockedAtLaunch: launched.security.locked,
			unlockedWith: "the Electron app's bcryptjs hash",
		},
		profiles,
		settings: { theme: unlocked.theme },
		siteData: seen,
		extensions: { electron: [EXTENSION], reinstalled: extensions },
	};
};

export default migration;
