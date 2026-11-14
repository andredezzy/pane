// Pane launched as its user launches it, which every scenario's launch is not
// (ADR 0121, pane/app/data_launch_win.cc): no --user-data-dir, no mock
// keychain, no test hook, on the computer's own %LOCALAPPDATA%, with a link,
// as the shell hands its default browser one, then another through the
// running Pane, as another app hands it one, and then its window closed. What
// it left is then read as a process of the user's reads files: the engine
// kept its data in the data folder's engine folder, the one chrome_elf names,
// and nowhere else; every file under the data folder is in Pane's formats or
// on ADR 0121's allow-list, and none holds a planted value
// (lib/file-audit.ts); no file there, nor one the user's profile or Pane's
// registry key changed meanwhile, holds or names a host or a value of the
// visits, Local State and the Default profile included; Local State names no
// profile of Pane's as used last; Crashpad kept nothing. Controls: two files
// planted plain, found, and the profile's own files read through its keys,
// which hold the hosts.
//
// The test VM's alone (docs/building.md, On Windows): it runs as the VM's
// user, whose %LOCALAPPDATA% holds no one's data, refuses on any other
// computer, and wherever a Pane's data is there already, and removes what it
// made. Never on the Mac, where its launch would be the user's own Pane's.
// Run in the console's session, as the scenarios are there: bun tests/run.ts
// --binary <folder>\chrome.exe --out <dir> tests/vm/user-launch.ts
import {
	type Dirent,
	existsSync,
	mkdirSync,
	readdirSync,
	readFileSync,
	rmSync,
	statSync,
	writeFileSync,
} from "node:fs";
import { join, relative, sep } from "node:path";
import { userSecret } from "../lib/engine-key";
import { audit, type Planted } from "../lib/file-audit";
import { bySystem, ENGINE_FOLDER, powershell } from "../lib/platform";
import { processTable, waitForExit } from "../lib/processes";
import { plainContent, profileKeys } from "../lib/profile-files";
import { expect, type Scenario } from "../run";

// The test VM, by its disk's serial (scripts/start.sh in the VM's folder:
// serial=PANEVM) and its machine's maker: never a computer someone works on,
// and never the Mac.
const inTheTestVM = bySystem({
	mac: async (): Promise<boolean> => false,
	windows: async (): Promise<boolean> => {
		const found = await powershell(
			"$disks = (Get-CimInstance Win32_DiskDrive).SerialNumber -join ','; $maker = (Get-CimInstance Win32_ComputerSystem).Manufacturer; \"$disks|$maker\"",
		);

		return /PANEVM/.test(found) && /QEMU/i.test(found);
	},
});

const UUID = /^[0-9a-f]{8}-(?:[0-9a-f]{4}-){3}[0-9a-f]{12}$/i;

const random = () => crypto.randomUUID().replaceAll("-", "").slice(0, 16);

// Every file and folder under `root`, not through a link or a junction.
function* entriesUnder(
	root: string,
): Generator<{ path: string; entry: Dirent }> {
	let entries: Dirent[];

	try {
		entries = readdirSync(root, { withFileTypes: true });
	} catch {
		return;
	}

	for (const entry of entries) {
		const path = join(root, entry.name);
		yield { path, entry };

		if (entry.isDirectory()) {
			yield* entriesUnder(path);
		}
	}
}

type Search = {
	files: number;
	bytes: number;
	held: string[];
	named: string[];
	unread: string[];
};

// Each file under `root` (those changed since `since`, when given), and every
// name, searched for each of `texts` in UTF-8 and UTF-16LE, as a process of
// the user's that reads files would; `skip` leaves a folder out.
function search(
	root: string,
	texts: string[],
	options: { since?: number; skip?: string[] } = {},
): Search {
	const found: Search = { files: 0, bytes: 0, held: [], named: [], unread: [] };

	const skipped = (path: string) =>
		(options.skip ?? []).some(
			(folder) =>
				path.toLowerCase() === folder.toLowerCase() ||
				path.toLowerCase().startsWith(`${folder.toLowerCase()}${sep}`),
		);

	for (const { path, entry } of entriesUnder(root)) {
		if (skipped(path)) {
			continue;
		}

		const name = relative(root, path).split(sep).join("/");

		for (const text of texts) {
			if (name.toLowerCase().includes(text.toLowerCase())) {
				found.named.push(`${name} names ${text}`);
			}
		}

		if (!entry.isFile()) {
			continue;
		}

		let bytes: Buffer;

		try {
			if (options.since && statSync(path).mtimeMs < options.since) {
				continue;
			}

			bytes = readFileSync(path);
		} catch (error) {
			found.unread.push(`${name}: ${(error as Error).message}`);

			continue;
		}

		found.files += 1;
		found.bytes += bytes.length;

		for (const text of texts) {
			if (bytes.includes(text, 0, "utf8")) {
				found.held.push(`${name} holds ${text} (UTF-8)`);
			}

			if (bytes.includes(text, 0, "utf16le")) {
				found.held.push(`${name} holds ${text} (UTF-16LE)`);
			}
		}
	}

	return found;
}

// What changed under `root` since `since`, as paths, folders' own listings
// aside, `skip` left out.
function changedSince(root: string, since: number, skip: string[]): string[] {
	const changed: string[] = [];

	for (const { path, entry } of entriesUnder(root)) {
		if (
			!entry.isFile() ||
			skip.some((folder) =>
				path.toLowerCase().startsWith(`${folder.toLowerCase()}${sep}`),
			)
		) {
			continue;
		}

		try {
			if (statSync(path).mtimeMs >= since) {
				changed.push(path);
			}
		} catch {}
	}

	return changed;
}

// The processes of this build, by its executable.
async function panesOf(binary: string) {
	return (await processTable()).filter(
		(row) => row.executable.toLowerCase() === binary.toLowerCase(),
	);
}

// The page a visit opens: it keeps a value of its own in each store a page
// keeps, its title and text hold one too, the server sets an HttpOnly cookie,
// and it says once all of it is kept.
const PAGE = (value: string) => `<!doctype html>
<html lang="en"><head><meta charset="utf-8"><title>Visit ${value}-title</title>
<link rel="icon" href="/icon.png"></head><body><p>${value}-text</p><script>
(async () => {
	const value = ${JSON.stringify(value)};
	localStorage.setItem("visit", value + "-localStorage");
	sessionStorage.setItem("visit", value + "-sessionStorage");
	document.cookie = "visit=" + value + "-cookie; max-age=86400; path=/";
	await new Promise((resolve, reject) => {
		const opened = indexedDB.open("visit", 1);
		opened.onupgradeneeded = () => opened.result.createObjectStore("items");
		opened.onerror = () => reject(opened.error);
		opened.onsuccess = () => {
			const tx = opened.result.transaction("items", "readwrite");
			tx.objectStore("items").put(value + "-indexedDB", "value");
			tx.oncomplete = () => { opened.result.close(); resolve(); };
			tx.onerror = () => reject(tx.error);
		};
	});
	const cache = await caches.open("visit");
	await cache.put("/entry", new Response(value + "-cacheStorage"));
	await fetch("/kept?value=" + value);
})().catch((error) => fetch("/failed?value=" + ${JSON.stringify(value)} + "&error=" + encodeURIComponent(String(error))));
</script></body></html>`;

// A 1×1 PNG, for the favicon the Favicons database keeps.
const ICON = Buffer.from(
	"iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==",
	"base64",
);

const STORES = [
	"title",
	"text",
	"localStorage",
	"sessionStorage",
	"cookie",
	"httpOnlyCookie",
	"indexedDB",
	"cacheStorage",
];

const userLaunch: Scenario = async (context) => {
	expect(
		await inTheTestVM(),
		"the user's launch runs in the test VM alone, on its own %LOCALAPPDATA%: not on this computer",
	);

	const localAppData = process.env.LOCALAPPDATA ?? "";
	const userProfile = process.env.USERPROFILE ?? "";
	const dataDir = join(localAppData, "Pane");
	const engine = join(dataDir, ENGINE_FOLDER);

	expect(
		localAppData !== "" && userProfile !== "" && !existsSync(dataDir),
		`a Pane's data is there already (${dataDir}): this check makes and removes its own, and never touches another's`,
	);

	expect(
		(await panesOf(context.binary)).length === 0,
		"a Pane of this build is running",
	);

	// The user's own environment, as the shell starts Pane with it: none of
	// the runner's PANE_ hooks, which no user's launch reads anyway.
	const env = Object.fromEntries(
		Object.entries(process.env).filter(
			([name]) => !name.toUpperCase().startsWith("PANE_"),
		),
	) as Record<string, string>;

	// The link the user's launch opens, and the one another app hands over.
	const visit = (name: string) => ({
		host: `user-launch-${name}-${random()}.localhost`,
		value: `ul${name}${random()}`,
	});

	const opened = visit("a");
	const handed = visit("b");
	const visits = [opened, handed];

	const kept = new Map<string, string>();
	const failed: string[] = [];
	// Every request the pages made, for what a run that fails says.
	const requests: string[] = [];

	const server = Bun.serve({
		hostname: "127.0.0.1",
		port: 0,
		fetch(request) {
			const url = new URL(request.url);
			const value = url.searchParams.get("value") ?? "";
			requests.push(`${request.headers.get("host")} ${url.pathname}`);

			if (url.pathname === "/visit") {
				return new Response(PAGE(value), {
					headers: {
						"Content-Type": "text/html; charset=utf-8",
						"Set-Cookie": `visit_http=${value}-httpOnlyCookie; Max-Age=86400; Path=/; HttpOnly`,
					},
				});
			}

			if (url.pathname === "/icon.png") {
				return new Response(ICON, { headers: { "Content-Type": "image/png" } });
			}

			if (url.pathname === "/kept") {
				kept.set(value, request.headers.get("host") ?? "");
			} else if (url.pathname === "/failed") {
				failed.push(`${value}: ${url.searchParams.get("error")}`);
			}

			return new Response("", { status: 204 });
		},
	});

	const found: Record<string, unknown> = { visits };

	const record = () =>
		writeFileSync(
			join(context.out, "user-launch-found.json"),
			`${JSON.stringify(found, null, "\t")}\n`,
		);

	const urlOf = (one: { host: string; value: string }) =>
		`http://${one.host}:${server.port}/visit?value=${one.value}`;

	const keptOne = async (one: { value: string }, what: string) => {
		const deadline = Date.now() + 120_000;

		while (!kept.has(one.value)) {
			expect(failed.length === 0, `the page failed: ${failed.join("; ")}`);

			if (Date.now() >= deadline) {
				// What ran, and what the data folder held, by name.
				found.requests = requests;

				found.processes = (await panesOf(context.binary)).map(
					(row) => row.command,
				);

				found.dataFolderListing = [...entriesUnder(dataDir)].map(({ path }) =>
					relative(dataDir, path),
				);

				record();
			}

			expect(Date.now() < deadline, `${what} kept nothing in 120 s`);
			await Bun.sleep(250);
		}
	};

	const controls = join(localAppData, `ul-controls-${random()}`);
	const emptyHome = context.tempDir("pane-user-launch-home-");
	const launched = Date.now();

	try {
		// The user's launch: the shell starts the default browser with the
		// link alone (its ProgID's command), in the user's environment.
		const first = Bun.spawn([context.binary, urlOf(opened)], {
			env,
			stdout: "ignore",
			stderr: Bun.file(join(context.out, "user-launch-first.log")),
		});

		await keptOne(opened, "the user's launch");

		const launchedPanes = await panesOf(context.binary);
		const browser = launchedPanes.find((row) => row.pid === first.pid);
		const helpers = launchedPanes.filter((row) => row.pid !== first.pid);

		found.commandLines = {
			browser: browser?.command,
			helpers: helpers.map((row) => row.command),
		};

		expect(
			browser !== undefined && !/--user-data-dir/i.test(browser.command),
			`the user's launch had --user-data-dir on its command line: ${browser?.command}`,
		);

		// The engine's helpers get its folder from the browser's command line
		// as Pane wrote it: the engine's folder chrome_elf named.
		const userDataDirs = new Set(
			helpers.flatMap((row) => {
				const match = /--user-data-dir="?([^"]+?)"?(?:\s+--|\s*$|")/i.exec(
					row.command,
				);

				return match?.[1] ? [match[1].toLowerCase()] : [];
			}),
		);

		found.helpersUserDataDirs = [...userDataDirs];

		expect(
			userDataDirs.size === 1 && userDataDirs.has(engine.toLowerCase()),
			`the engine's helpers' folder: ${JSON.stringify([...userDataDirs])}, where the data's is ${engine}`,
		);

		// Another app hands it a link: the shell starts the default browser
		// again, which hands the link to the running Pane and ends.
		const second = Bun.spawn([context.binary, urlOf(handed)], {
			env,
			stdout: "ignore",
			stderr: Bun.file(join(context.out, "user-launch-second.log")),
		});

		const secondEnded = await Promise.race([
			second.exited.then(() => true),
			Bun.sleep(60_000).then(() => false),
		]);

		found.secondLaunch = { ended: secondEnded, exitCode: second.exitCode };

		expect(
			secondEnded,
			"the second launch never handed its link over and ended",
		);

		await keptOne(handed, "the link handed to the running Pane");

		expect(
			(await panesOf(context.binary)).filter(
				(row) => !/--type=/.test(row.command),
			).length === 1,
			"a second browser process runs: the link wasn't handed to the first",
		);

		// The window closed, as its X closes it: Pane quits (ADR 0053).
		await Bun.sleep(3_000);

		await powershell(
			"(Get-Process -Id ([int]$env:PANE_PID)).CloseMainWindow() | Out-Null",
			{ PANE_PID: String(first.pid) },
		);

		expect(
			await waitForExit(first.pid, 60_000),
			"Pane didn't quit when its window closed",
		);

		const quitBy = Date.now() + 30_000;

		while ((await panesOf(context.binary)).length > 0 && Date.now() < quitBy) {
			await Bun.sleep(500);
		}

		expect(
			(await panesOf(context.binary)).length === 0,
			"Pane's helpers still run 30 s after it quit",
		);

		// Its last writes made, its files let go.
		await Bun.sleep(3_000);

		// Where it all went.
		const engineFolders = readdirSync(dataDir);

		const profiles = existsSync(engine)
			? readdirSync(engine).filter((name) => UUID.test(name))
			: [];

		found.dataFolder = engineFolders;
		found.profiles = profiles;

		expect(
			existsSync(join(engine, "Local State")) && profiles.length === 1,
			`the engine kept its data elsewhere than ${engine}: the data folder holds ${JSON.stringify(engineFolders)}, its profiles ${JSON.stringify(profiles)}`,
		);

		expect(
			!existsSync(join(dataDir, "Engine")),
			"an Engine folder beside the engine's",
		);

		const profile = profiles[0] ?? "";

		// The controls: two files planted plain where the search reads.
		mkdirSync(controls, { recursive: true });
		writeFileSync(join(controls, "utf8.txt"), `visited ${opened.host}\n`);

		writeFileSync(
			join(controls, "utf16.txt"),
			Buffer.from(`visited ${handed.host}\n`, "utf16le"),
		);

		const hosts = visits.map((visit) => visit.host);

		const values = visits.flatMap((visit) =>
			STORES.map((store) => `${visit.value}-${store}`),
		);

		const texts = [...hosts, ...values];

		// ADR 0121's audit of the data folder: Pane's formats, the allow-list,
		// no planted value, no name giving a site away.
		const planted: Planted = {
			values: Object.fromEntries(
				visits.flatMap((one) =>
					STORES.map((store) => [
						`${one === opened ? "opened" : "handed"}.${store}`,
						`${one.value}-${store}`,
					]),
				),
			),
			hosts,
			origins: visits.map((one) => `http://${one.host}:${server.port}`),
			urls: visits.map(urlOf),
		};

		const audited = audit(
			{ dataRoot: dataDir, home: emptyHome, temporary: [] },
			planted,
		);

		found.audit = {
			files: audited.files.length,
			plain: audited.plain.map(({ path, kind, plain }) => ({
				path,
				kind,
				plain,
			})),
			planted: audited.planted.map(({ path, planted }) => ({ path, planted })),
			named: audited.named,
		};

		writeFileSync(
			join(context.out, "user-launch-inventory.json"),
			`${JSON.stringify(
				audited.files
					.map((file) => ({
						...file,
						path: file.path.replaceAll(profile, "<id>"),
					}))
					.sort((a, b) => a.path.localeCompare(b.path)),
				null,
				"\t",
			)}\n`,
		);

		// The disk check: the data folder whole, and what the user's profile
		// changed meanwhile, every file and name, for each host and value.
		const inData = search(dataDir, texts);

		const inProfile = search(userProfile, texts, {
			since: launched,
			skip: [dataDir, emptyHome],
		});

		const changed = changedSince(userProfile, launched, [dataDir, emptyHome]);

		// A folder of Pane's or of Chromium's, as the install mode would name
		// one, outside the data folder: what the engine kept there, plain,
		// before the engine's folder was chrome_elf's.
		const panesElsewhere = changed.filter((path) =>
			/(^|[\\/])(pane|chromium)([\\/]|$)/i.test(relative(userProfile, path)),
		);

		// Pane's registry key, which a test copy shares with the user's Pane.
		const registry = await powershell(
			"if (Test-Path 'HKCU:\\Software\\Pane') { reg query 'HKCU\\Software\\Pane' /s } else { 'none' }",
		);

		const inRegistry = texts.filter((text) =>
			registry.toLowerCase().includes(text.toLowerCase()),
		);

		// The engine's root, Local State and the Default profile, holds none of
		// the browsing (ADR 0113).
		const atTheRoot = [...inData.held, ...inData.named].filter(
			(line) =>
				line.startsWith(`${ENGINE_FOLDER}/Local State `) ||
				line.startsWith(`${ENGINE_FOLDER}/Default/`),
		);

		const localState = JSON.parse(
			readFileSync(join(engine, "Local State"), "utf8"),
		) as {
			profile?: {
				last_used?: string;
				info_cache?: Record<string, { active_time?: number }>;
			};
			tab_stats?: unknown;
		};

		const crashpad = [...entriesUnder(join(engine, "Crashpad"))].map(
			({ path }) => relative(dataDir, path),
		);

		const dumps = [
			...[...entriesUnder(dataDir)].map(({ path }) => path),
			...changed,
		].filter((path) => path.toLowerCase().endsWith(".dmp"));

		// What the engine kept, read through the profile's keys under the
		// user's own secret.
		const keys = profileKeys(dataDir, profile, null, userSecret(engine));
		const throughTheKeys: string[] = [];

		for (const { path, entry } of entriesUnder(join(engine, profile))) {
			if (!entry.isFile()) {
				continue;
			}

			try {
				const plain = plainContent(path, keys);

				for (const host of hosts) {
					if (plain.includes(host, 0, "latin1")) {
						throughTheKeys.push(
							`${relative(join(engine, profile), path).split(sep).join("/")} keeps ${host}`,
						);
					}
				}
			} catch {}
		}

		const ours = (lines: string[]) =>
			lines.filter(
				(line) =>
					!line.startsWith(
						`${relative(userProfile, controls).split(sep).join("/")}/`,
					),
			);

		Object.assign(found, {
			inData,
			inProfile,
			changedInProfile: changed.map((path) => relative(userProfile, path)),
			panesElsewhere: panesElsewhere.map((path) => relative(userProfile, path)),
			registry: registry.trim(),
			inRegistry,
			atTheRoot,
			localState: {
				lastUsed: localState.profile?.last_used,
				timesOfUse: Object.values(localState.profile?.info_cache ?? {}).filter(
					(info) => info.active_time !== undefined,
				).length,
				tabStats: localState.tab_stats !== undefined,
			},
			crashpad,
			dumps,
			throughTheKeys,
		});

		record();

		expect(
			inProfile.held.some((line) => line.includes("/utf8.txt holds")) &&
				inProfile.held.some((line) => line.includes("/utf16.txt holds")),
			`the controls weren't found: ${JSON.stringify(inProfile.held)}`,
		);

		expect(
			hosts.every((host) =>
				throughTheKeys.some(
					(line) => line.startsWith("History ") && line.endsWith(host),
				),
			),
			`the profile's History, through its keys, keeps no host of a visit: ${JSON.stringify(throughTheKeys)}`,
		);

		expect(
			audited.files.length > 50 &&
				audited.plain.length === 0 &&
				audited.planted.length === 0 &&
				audited.named.length === 0,
			`the data folder's audit: ${JSON.stringify(found.audit)}`,
		);

		expect(
			atTheRoot.length === 0,
			`the engine's root holds the browsing: ${atTheRoot.join("; ")}`,
		);

		expect(
			inData.held.length === 0 && inData.named.length === 0,
			`a host or value on disk in the data folder: ${JSON.stringify({ held: inData.held, named: inData.named })}`,
		);

		expect(
			ours(inProfile.held).length === 0 && ours(inProfile.named).length === 0,
			`a host or value outside the data folder: ${JSON.stringify({ held: ours(inProfile.held), named: ours(inProfile.named) })}`,
		);

		expect(
			panesElsewhere.length === 0,
			`Pane's or Chromium's files outside the data folder: ${JSON.stringify(found.panesElsewhere)}`,
		);

		expect(
			inRegistry.length === 0,
			`a host or value in Pane's registry key: ${JSON.stringify(inRegistry)}`,
		);

		expect(
			localState.profile?.last_used === "Default" &&
				(found.localState as { timesOfUse: number }).timesOfUse === 0 &&
				localState.tab_stats === undefined,
			`Local State after the quit: ${JSON.stringify(found.localState)}`,
		);

		expect(
			crashpad.length === 0 && dumps.length === 0,
			`Crashpad's database or a minidump: ${JSON.stringify({ crashpad, dumps })}`,
		);
	} finally {
		record();
		server.stop(true);

		// Nothing of it stays: its processes, its data, the controls.
		for (const row of await panesOf(context.binary)) {
			await Bun.$`taskkill /F /T /PID ${row.pid}`.quiet().nothrow();
		}

		for (const folder of [dataDir, controls]) {
			for (let attempt = 0; attempt < 20 && existsSync(folder); attempt++) {
				try {
					rmSync(folder, { recursive: true, force: true });
				} catch {
					await Bun.sleep(500);
				}
			}
		}

		found.removed = !existsSync(dataDir) && !existsSync(controls);
		record();
	}
};

export default userLaunch;
