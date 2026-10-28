import { mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { type AppState, launchPane, tabOf } from "../lib/pane";
import { startHttpProxy, startSocks5Proxy } from "../lib/proxies";
import { WindowWatcher } from "../lib/surfaces";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

const CREDENTIALS = { username: "pane", password: "proxy-secret" };

// Per-profile proxies against local proxies the scenario starts: profile A
// through an HTTP proxy with Basic auth (signed in with the profile's stored
// credentials, no prompt), profile C through a SOCKS5 proxy with a password
// (through the app's relay), and profile B direct. Each proxy logs what it
// carried; A's WebRTC gathers no address of the machine. The passwords are
// kept in the keychain, never in profiles.json (scenarios run with the mock
// keychain, a file of the data directory): a password the file still holds,
// as the Electron app's did, signs in, moves there, and signs in again after
// a relaunch.

type SavedProxy = { password: string | null } | null;

function savedPasswords(dataDir: string) {
	const read = <T>(name: string): T | null => {
		try {
			return (
				JSON.parse(readFileSync(join(dataDir, name), "utf8")) as { state: T }
			).state;
		} catch {
			return null;
		}
	};

	return {
		inFile: (
			read<{ profiles: { id: string; proxy: SavedProxy }[] }>("profiles.json")
				?.profiles ?? []
		)
			.filter((profile) => profile.proxy?.password)
			.map((profile) => profile.id),
		inKeychain: Object.keys(
			read<Record<string, string>>("mock-keychain.json") ?? {},
		),
	};
}

const proxy: Scenario = async (context) => {
	const fixturePort = Number(new URL(context.fixtures.origin).port);
	const http = await startHttpProxy(fixturePort, CREDENTIALS);
	const socks = await startSocks5Proxy(fixturePort, CREDENTIALS);

	try {
		const { app, has } = await launchTraced(context, "proxy", {
			env: { PANE_PROXY_TEST_URL: "http://check.pane.test/ip" },
		});

		const watcher = new WindowWatcher(app).start();
		const a = await app.createProfile("Proxied HTTP", "BLUE");
		const b = await app.createProfile("Direct", "EMERALD");
		const c = await app.createProfile("Proxied SOCKS", "VIOLET");

		await app.send("setProfileProxy", {
			profile: a,
			proxy: {
				proxyType: "HTTP",
				host: "127.0.0.1",
				port: http.port,
				...CREDENTIALS,
			},
		});

		await app.send("setProfileProxy", {
			profile: c,
			proxy: {
				proxyType: "SOCKS5",
				host: "127.0.0.1",
				port: socks.port,
				...CREDENTIALS,
			},
		});

		// The connection tests, as the sidebar shows them.
		const tested = await app.waitFor(
			"both proxies tested",
			(state) =>
				["connected", "failed"].includes(statusOf(state, a) ?? "") &&
				["connected", "failed"].includes(statusOf(state, c) ?? ""),
			30_000,
		);

		const pageA = "http://a.pane.test/proxy-check.html";
		const tabA = await app.openTab(a, pageA);

		const loadedA = await app.waitFor(
			"A's page through its proxy",
			(state) => tabOf(state, tabA)?.title === "Proxy check",
			30_000,
		);

		const pageC = "http://c.pane.test/proxy-check.html";
		const tabC = await app.openTab(c, pageC);

		await app.waitFor(
			"C's page through its proxy",
			(state) => tabOf(state, tabC)?.title === "Proxy check",
			30_000,
		);

		const carriedBefore = http.log.length + socks.log.length;
		const pageB = context.fixtures.url("proxy-check.html?direct");
		const tabB = await app.openTab(b, pageB);
		const directB = await app.page(tabB, pageB);
		const carriedAfter = http.log.length + socks.log.length;

		// WebRTC: A sends all UDP to its proxy, which means none; B hides its
		// private interfaces.
		const webRTCA = await (await app.page(tabA, pageA)).evaluate<string[]>(
			"gatherCandidates()",
		);

		const webRTCB = await directB.evaluate<string[]>("gatherCandidates()");
		const network = await app.state();

		const httpForA = http.log.filter((entry) =>
			entry.target.includes("a.pane.test"),
		);

		const socksForC = socks.log.filter((entry) =>
			entry.target.startsWith("c.pane.test"),
		);

		const privateAddress =
			/\b(10|127|192\.168|172\.(1[6-9]|2\d|3[01]))\.\d+\.\d+/;

		expect(
			statusOf(tested, a) === "connected",
			`A's proxy test: ${statusOf(tested, a)}`,
		);

		expect(
			statusOf(tested, c) === "connected",
			`C's proxy test: ${statusOf(tested, c)}`,
		);

		expect(
			httpForA.length > 0 &&
				httpForA.every((entry) => entry.user === CREDENTIALS.username),
			`the HTTP proxy carried ${JSON.stringify(http.log)}`,
		);

		expect(
			socksForC.length > 0 &&
				socksForC.every((entry) => entry.user === CREDENTIALS.username),
			`the SOCKS5 proxy carried ${JSON.stringify(socks.log)}`,
		);

		expect(carriedAfter === carriedBefore, "B's page went through a proxy");

		expect(
			!has("PaneWebContentsDelegate.authenticate") || network.sheet === null,
			"the proxy asked the user",
		);

		expect(
			!webRTCA.some(
				(candidate) =>
					privateAddress.test(candidate) || / typ host /.test(candidate),
			),
			`A's WebRTC gathered ${JSON.stringify(webRTCA)}`,
		);

		expect(
			!webRTCB.some((candidate) => privateAddress.test(candidate)),
			`B's WebRTC exposed ${JSON.stringify(webRTCB)}`,
		);

		// A SOCKS5 proxy with credentials on a port past 65535 (a hand-edited
		// profiles.json, say): its relay refuses each connection rather than
		// trapping, so the test fails, the page does not load, and Pane stays
		// up with every other profile.
		const d = await app.createProfile("Bad port", "AMBER");

		await app.send("setProfileProxy", {
			profile: d,
			proxy: {
				proxyType: "SOCKS5",
				host: "127.0.0.1",
				port: 70_000,
				...CREDENTIALS,
			},
		});

		const tabD = await app.openTab(d, "http://d.pane.test/proxy-check.html");

		const badPort = await app.waitFor(
			"D's proxy test failed",
			(state) => statusOf(state, d) === "failed",
			30_000,
		);

		await Bun.sleep(2_000);
		const afterBadPort = await app.state();

		expect(
			tabOf(afterBadPort, tabD)?.title !== "Proxy check" &&
				tabOf(afterBadPort, tabA)?.title === "Proxy check",
			`the bad port: ${JSON.stringify(tabOf(afterBadPort, tabD))}`,
		);

		const windows = await watcher.stop();

		expect(
			windows.others.length === 0,
			`Chrome windows appeared: ${JSON.stringify(windows.others)}`,
		);

		await app.send("save");
		const passwords = savedPasswords(app.dataDir);

		expect(
			passwords.inFile.length === 0 &&
				[a, c, d].every((id) => passwords.inKeychain.includes(id)),
			`the passwords: ${JSON.stringify(passwords)}`,
		);

		await app.browser.close();

		// The Electron app's profiles.json, with its proxy's password.
		const legacyDir = mkdtempSync(join(tmpdir(), "pane-proxy-legacy-"));
		const legacyPage = "http://legacy.pane.test/proxy-check.html";

		writeFileSync(
			join(legacyDir, "profiles.json"),
			JSON.stringify({
				state: {
					profiles: [
						{
							id: "legacy",
							name: "Legacy",
							color: "BLUE",
							group: null,
							fingerprint: null,
							proxy: {
								proxyType: "HTTP",
								host: "127.0.0.1",
								port: http.port,
								...CREDENTIALS,
							},
							keepLoaded: false,
							tabs: [
								{ id: "legacy-tab", url: legacyPage, title: "", favicon: "" },
							],
							createdAt: "2026-10-27T00:00:00.000Z",
							updatedAt: "2026-10-27T00:00:00.000Z",
						},
					],
				},
				version: 0,
			}),
		);

		const legacyRuns: unknown[] = [];

		for (const run of ["moved", "read back"]) {
			const legacy = await launchPane(context, { dataDir: legacyDir });
			await legacy.send("select", { tab: "legacy-tab" });

			const signedIn = await legacy.waitFor(
				`the legacy profile's page (${run})`,
				(state) => tabOf(state, "legacy-tab")?.title === "Proxy check",
				30_000,
			);

			await legacy.send("save");

			legacyRuns.push({
				run,
				sheet: signedIn.sheet,
				passwords: savedPasswords(legacyDir),
			});

			await legacy.send("quit");
			await Bun.sleep(1_000);
		}

		rmSync(legacyDir, { recursive: true, force: true });

		const legacyMoved = legacyRuns.every(
			(entry) =>
				(entry as { sheet: unknown }).sheet === null &&
				JSON.stringify((entry as { passwords: unknown }).passwords) ===
					JSON.stringify({ inFile: [], inKeychain: ["legacy"] }),
		);

		expect(legacyMoved, `the legacy password: ${JSON.stringify(legacyRuns)}`);

		return {
			proxies: {
				http: { port: http.port, log: http.log },
				socks5: { port: socks.port, log: socks.log },
			},
			status: {
				a: statusOf(tested, a),
				c: statusOf(tested, c),
				badPort: statusOf(badPort, d),
			},
			network: Object.fromEntries(
				network.profiles.map((profile) => [profile.name, profile.network]),
			),
			pages: { a: tabOf(loadedA, tabA)?.title, b: tabOf(network, tabB)?.url },
			webRTC: { a: webRTCA, b: webRTCB },
			windows,
			passwords,
			legacyPassword: legacyRuns,
		};
	} finally {
		http.stop();
		socks.stop();
	}
};

function statusOf(state: AppState, profileID: string): string | null {
	return (
		state.profiles.find((profile) => profile.id === profileID)?.proxyStatus ??
		null
	);
}

export default proxy;
