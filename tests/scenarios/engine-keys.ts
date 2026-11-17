import { existsSync, readdirSync, readFileSync } from "node:fs";
import { join } from "node:path";
import { engineKey, keyForms } from "../lib/engine-key";
import { setPinBeforeLaunch, tabOf } from "../lib/pane";
import { poll } from "../lib/poll";
import {
	commandLinesAndEnvironments,
	helperPid,
	waitForExit,
} from "../lib/processes";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// A profile's engine key (ADR 0121) reaches the network service and the Storage
// Service, a restart of either gets it back and loses nothing, it is on no
// command line, in no environment, and in no file, and every process's registry
// is empty once a lock has closed the data; the PIN brings it all back, and the
// tab it brings back loads again by itself when the network service's crash
// cuts its first load short. On
// Windows the PIN's seal takes the TPM's share too (U5), which the key's
// derivation here asks the chip for, with the PIN.

function* filesUnder(root: string): Generator<string> {
	for (const entry of readdirSync(root, { withFileTypes: true })) {
		const path = join(root, entry.name);

		if (entry.isDirectory()) {
			yield* filesUnder(path);
		} else if (entry.isFile()) {
			yield path;
		}
	}
}

const engineKeys: Scenario = async (context) => {
	const dataDir = context.tempDir("pane-engine-keys-");
	await setPinBeforeLaunch(context, dataDir, "1234");

	const { app, trace } = await launchTraced(context, "engine-keys", {
		dataDir,
	});

	await app.send("setLockAfter", { minutes: null });
	await app.send("pinKeys", { keys: "1234" });

	await app.waitFor(
		"unlocked, the volume attached",
		(state) => !state.security.locked && state.security.data.attached,
		60_000,
	);

	const profile = await app.createProfile("Keys");
	const { port } = new URL(context.fixtures.origin);
	const url = `http://keys.localhost:${port}/smoke.html`;
	const { tab, page: firstPage } = await app.openPage(profile, url);
	let page = firstPage;

	await page.evaluate(
		`document.cookie = "kept=cookie-value; max-age=86400"; localStorage.setItem("kept", "storage-value")`,
	);

	// The key, in every form, on no command line and in no environment.
	const forms = keyForms(engineKey(dataDir, profile, "1234"));

	const processes = (await commandLinesAndEnvironments()).filter((row) =>
		row.includes(dataDir.replace(/^\/private/, "")),
	);

	expect(processes.length >= 3, `only ${processes.length} processes found`);

	// The environments are read: the browser's holds what the runner gave it.
	expect(
		processes.some((row) => row.includes("PANE_AUTOMATION_DIR=")),
		"no process's environment was read",
	);

	for (const row of processes) {
		for (const form of forms) {
			expect(
				!row.includes(form.toString("latin1")),
				"the key is on a command line or in an environment",
			);
		}
	}

	// In no file, once what is written has reached the disk.
	await Bun.sleep(32_000); // The cookie store commits within 30 s.

	// Windows refuses another process's open of a file a process of Pane's
	// holds shut: Chromium locks the cookies' database there against other
	// apps. Those are searched once Pane has quit.
	const heldShut: string[] = [];

	for (const file of filesUnder(dataDir)) {
		let bytes: Buffer;

		try {
			bytes = readFileSync(file);
		} catch {
			heldShut.push(file);

			continue;
		}

		for (const form of forms) {
			expect(!bytes.includes(form), `the key is in ${file}`);
		}
	}

	const pidOf = (kind: string) => helperPid(dataDir, kind);

	const readBack = async () =>
		page.evaluate<{ cookie: string; storage: string | null }>(
			`({ cookie: document.cookie, storage: localStorage.getItem("kept") })`,
		);

	// A network service that restarts gets its key back, and has the cookies.
	const network = await pidOf("network.mojom.NetworkService");
	process.kill(network, "SIGKILL");

	await poll(
		"the network service back",
		async () => {
			const next = await pidOf("network.mojom.NetworkService");

			return next > 0 && next !== network && next;
		},
		15_000,
		200,
	);

	await app.send("reload", { tab });
	await Bun.sleep(1_000);
	page = await app.page(tab, url);
	const cookiesBack = await readBack();

	expect(
		cookiesBack.cookie.includes("kept=cookie-value"),
		`the cookie after the network service restarted: ${cookiesBack.cookie}`,
	);

	// A Storage Service that restarts gets its key back, and has what it stored.
	const storage = await pidOf("storage.mojom.StorageService");
	process.kill(storage, "SIGKILL");

	await poll(
		"the Storage Service back",
		async () => {
			await app.send("reload", { tab });
			const next = await pidOf("storage.mojom.StorageService");

			return next > 0 && next !== storage && next;
		},
		15_000,
		500,
	);

	await Bun.sleep(1_000);
	page = await app.page(tab, url);
	const storageBack = await readBack();

	expect(
		storageBack.storage === "storage-value",
		`local storage after the Storage Service restarted: ${storageBack.storage}`,
	);

	await page.evaluate(`localStorage.setItem("kept", "storage-value-2")`);
	await Bun.sleep(3_000);

	// The lock: every registry empty once the volume has detached.
	await app.send("lockNow");

	const dropped = await poll(
		"every process's registry empty",
		async () =>
			trace().find(
				(line) => line.name === "PaneBrowserApplication.engineKeysDropped",
			) ?? false,
		30_000,
		200,
	);

	expect(
		dropped.args.browser === 0 &&
			dropped.args.network === 0 &&
			dropped.args.storage === 0,
		`a process kept its keys: ${JSON.stringify(dropped.args)}`,
	);

	// The app has not crashed, nothing is open, and the PIN brings it back.
	await app.send("pinKeys", { keys: "1234" });

	await app.waitFor(
		"unlocked again, the tab back",
		(state) =>
			!state.security.locked &&
			state.security.data.attached &&
			tabOf(state, tab)?.url === url &&
			tabOf(state, tab)?.loading === false,
		60_000,
	);

	page = await app.page(tab, url);
	const unlockedBack = await readBack();

	expect(
		unlockedBack.cookie.includes("kept=cookie-value") &&
			unlockedBack.storage === "storage-value-2",
		`after the lock: ${JSON.stringify(unlockedBack)}`,
	);

	// The tab the unlock brings back, its first load lost as the network
	// service crashed (W9a: Windows' error reporting held the dead service
	// past the PIN, and the tab stayed blank, its load never tried again):
	// it loads by itself once the service is back. Its response held 6 s, and
	// never kept by the HTTP cache (no-store), as the restore would otherwise
	// load it from there at once; the service killed meanwhile.
	const slowURL = `http://keys.localhost:${port}/headers?format=html&delay=6000`;
	await app.send("navigate", { input: slowURL });

	await app.waitFor(
		"the slow page",
		(state) =>
			tabOf(state, tab)?.url === slowURL &&
			tabOf(state, tab)?.loading === false,
		30_000,
	);

	await app.send("lockNow");

	await poll(
		"the keys dropped at the second lock",
		async () =>
			trace().filter(
				(line) => line.name === "PaneBrowserApplication.engineKeysDropped",
			).length >= 2,
		30_000,
		200,
	);

	await app.send("pinKeys", { keys: "1234" });

	await app.waitFor(
		"the tab's page back, its first load under way",
		(state) =>
			!state.security.locked &&
			tabOf(state, tab)?.url === slowURL &&
			tabOf(state, tab)?.loaded === true,
		60_000,
	);

	// Its load shows from its start, as any load's does: the row's and the
	// toolbar's loading and the bar, while the server holds the response.
	// Chrome's Navigate starts a restored tab's load before Pane observes the
	// page, which missed its start and showed nothing until a response (W9c).
	await app.waitFor(
		"the restored page's load showing",
		(state) => tabOf(state, tab)?.loading === true && state.progressBar.started,
		2_000,
	);

	// Its request at the server, which holds it 6 s.
	await Bun.sleep(1_500);
	process.kill(await pidOf("network.mojom.NetworkService"), "SIGKILL");
	page = await app.page(tab, slowURL);

	expect(
		await page.evaluate<boolean>('typeof window.headers === "object"'),
		"the tab the unlock brought back stayed blank after the network service's crash",
	);

	await app.quit();
	await waitForExit(app.browser.pid, 30_000);

	for (const file of heldShut.filter((path) => existsSync(path))) {
		const bytes = readFileSync(file);

		for (const form of forms) {
			expect(!bytes.includes(form), `the key is in ${file}`);
		}
	}
};

export default engineKeys;
