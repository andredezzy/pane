import { existsSync, readFileSync } from "node:fs";
import { homedir } from "node:os";
import { join } from "node:path";
import { Cdp, openPage } from "../lib/cdp";
import { type AppState, type PaneApp, tabOf } from "../lib/pane";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// Google sign-in. Google's identifier page loads in a profile, with and
// without a fingerprint, and does not reject the engine (no sign-in is
// attempted). Then the fallback, against stand-ins for Google's accounts
// pages (PANE_GOOGLE_ACCOUNTS_ORIGIN): a /rejected page takes the tab over
// and opens Chrome for Testing on the sign-in page; Transfer finds no
// session at first, then brings back the Google cookies the scenario put in
// that Chrome (made-up values, on a path no Google page uses), and continues
// to Google. Sign out removes them and the storage Google's widgets use.

const CHROME = join(
	homedir(),
	"chromium/cft/chrome-mac-arm64/Google Chrome for Testing.app/Contents/MacOS/Google Chrome for Testing",
);

const WINDOWS = {
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
};

// A path no Google page is under: the profile never sends these cookies to
// Google when the transfer continues there.
const COOKIE_PATH = "/pane-scenario";

const SESSION = [
	{ name: "SID", domain: ".google.com", sameSite: "Lax" },
	{ name: "__Secure-3PSID", domain: ".google.com", sameSite: "None" },
	{
		name: "PANE_SCENARIO_HOST",
		url: `https://accounts.google.com${COOKIE_PATH}`,
		sameSite: "Lax",
	},
];

// sessions.ts's account cookies, and its Google domains.
const AUTH_COOKIES = new Set([
	"SID",
	"HSID",
	"SSID",
	"APISID",
	"SAPISID",
	"__Secure-1PSID",
	"__Secure-3PSID",
	"__Secure-1PAPISID",
	"__Secure-3PAPISID",
]);

function isGoogleDomain(domain: string): boolean {
	return /(^|\.)(google|youtube)\.[a-z]{2,3}(\.[a-z]{2,3})?$/i.test(domain);
}

async function pageAt(app: PaneApp, prefix: string) {
	const deadline = Date.now() + 30_000;

	while (Date.now() < deadline) {
		const { targetInfos } = await app.browser.cdp.send<{
			targetInfos: { targetId: string; type: string; url: string }[];
		}>("Target.getTargets");

		const target = targetInfos.find(
			(info) => info.type === "page" && info.url.startsWith(prefix),
		);

		if (target) {
			return openPage(app.browser.cdp, "about:blank", target.targetId);
		}

		await Bun.sleep(250);
	}

	throw new Error(`no page at ${prefix}`);
}

// Google's identifier page, as the profile gets it.
async function identifierPage(app: PaneApp, profile: string) {
	const tab = await app.openTab(profile, "https://accounts.google.com/");

	const loaded = await app.waitFor(
		"accounts.google.com loaded",
		(state) => {
			const current = tabOf(state, tab);

			return (
				!!current &&
				!current.loading &&
				current.url.startsWith("https://accounts.google.com/") &&
				current.url !== "https://accounts.google.com/"
			);
		},
		45_000,
	);

	const page = await pageAt(app, "https://accounts.google.com/");
	const deadline = Date.now() + 20_000;
	let report = { url: "", identifier: false, title: "" };

	while (Date.now() < deadline) {
		report = await page.evaluate<typeof report>(
			`({ url: location.href, identifier: !!document.querySelector('input[type="email"], #identifierId'), title: document.title })`,
		);

		if (report.identifier) {
			break;
		}

		await Bun.sleep(500);
	}

	return {
		tab,
		url: new URL(report.url),
		identifier: report.identifier,
		title: report.title,
		state: tabOf(loaded, tab),
	};
}

function signInOf(state: AppState, tab: string) {
	return tabOf(state, tab)?.googleSignIn ?? null;
}

function devToolsPort(chromeProfile: string): number | null {
	const file = join(chromeProfile, "DevToolsActivePort");

	return existsSync(file)
		? Number(readFileSync(file, "utf8").split("\n")[0]) || null
		: null;
}

async function chromeAt(port: number): Promise<Cdp> {
	const version = (await (
		await fetch(`http://127.0.0.1:${port}/json/version`)
	).json()) as {
		webSocketDebuggerUrl: string;
	};

	return Cdp.connect(version.webSocketDebuggerUrl);
}

const google: Scenario = async (context) => {
	expect(existsSync(CHROME), `no Chrome for Testing at ${CHROME}`);

	const accounts = context.fixtures.origin;

	const { app, has, trace } = await launchTraced(context, "google", {
		env: { PANE_GOOGLE_ACCOUNTS_ORIGIN: accounts },
	});

	// Google accepts the engine: its identifier page, no /rejected.
	const plain = await app.createProfile("Google", "BLUE");
	const windows = await app.createProfile("Google Windows", "EMERALD");
	await app.send("setProfileFingerprint", {
		profile: windows,
		fingerprint: WINDOWS,
	});

	const accepted = {
		plain: await identifierPage(app, plain),
		windows: await identifierPage(app, windows),
	};

	for (const [name, result] of Object.entries(accepted)) {
		expect(
			result.url.hostname === "accounts.google.com" &&
				!result.url.pathname.includes("/rejected") &&
				result.identifier,
			`${name}: Google showed ${result.url.href} (identifier field: ${result.identifier})`,
		);
	}

	// The fallback.
	await app.send("setChromiumPath", { path: CHROME });
	const profile = await app.createProfile("Fallback", "VIOLET");
	const rejected = `${accounts}/v3/signin/rejected?continue=${encodeURIComponent("https://www.google.com/")}`;
	const tab = await app.openTab(profile, rejected);

	const intercepted = await app.waitFor(
		"the transfer page with its Chrome",
		(state) =>
			!!signInOf(state, tab)?.chromeProfile &&
			devToolsPort(signInOf(state, tab)?.chromeProfile ?? "") !== null,
		30_000,
	);

	// The transfer page, as the window draws it.
	await app.send("render", {
		path: join(context.out, "google-transfer-page.png"),
	});

	const chromeProfile = signInOf(intercepted, tab)?.chromeProfile ?? "";
	const port = devToolsPort(chromeProfile) ?? 0;
	const chrome = await chromeAt(port);
	const deadline = Date.now() + 20_000;
	let chromePage = "";

	while (
		Date.now() < deadline &&
		!chromePage.startsWith(`${accounts}/ServiceLogin`)
	) {
		const { targetInfos } = await chrome.send<{
			targetInfos: { type: string; url: string }[];
		}>("Target.getTargets");
		chromePage = targetInfos.find((info) => info.type === "page")?.url ?? "";
		await Bun.sleep(250);
	}

	expect(
		chromePage ===
			`${accounts}/ServiceLogin?continue=${"https://www.google.com/"}` ||
			chromePage ===
				`${accounts}/ServiceLogin?continue=${encodeURIComponent("https://www.google.com/")}`,
		`Chrome opened ${chromePage}`,
	);

	// Transfer before signing in: nothing to bring back.
	await app.send("googleTransfer", { tab });

	const empty = await app.waitFor(
		"the transfer's error",
		(state) => !!signInOf(state, tab)?.error,
		20_000,
	);

	expect(
		signInOf(empty, tab)?.error ===
			"No Google cookies found. Make sure you completed sign-in in the Chrome window." &&
			signInOf(empty, tab)?.transferring === false,
		`the empty transfer said ${JSON.stringify(signInOf(empty, tab))}`,
	);

	// "Signs in": the session Google would have set, made up.
	const expires = Math.floor(Date.now() / 1000) + 3600;

	await chrome.send("Storage.setCookies", {
		cookies: SESSION.map((cookie) => ({
			name: cookie.name,
			value: "pane-scenario",
			...(cookie.url ? { url: cookie.url } : { domain: cookie.domain }),
			path: COOKIE_PATH,
			secure: true,
			httpOnly: true,
			sameSite: cookie.sameSite,
			expires,
		})),
	});

	chrome.close();

	await app.send("googleTransfer", { tab });

	const transferred = await app.waitFor(
		"the tab continued to Google",
		(state) =>
			signInOf(state, tab) === null &&
			(tabOf(state, tab)?.url ?? "").startsWith("https://www.google.com/"),
		30_000,
	);

	await Bun.sleep(3_000);

	const chromeQuit = await fetch(`http://127.0.0.1:${port}/json/version`).then(
		() => false,
		() => true,
	);

	await app.send("readCookies", { profile });

	const imported = await app.waitFor(
		"the profile's cookies, signed in",
		(state) => {
			const current = state.profiles.find((entry) => entry.id === profile);

			return !!current?.cookies && current.googleSignedIn;
		},
		20_000,
	);

	const cookies =
		imported.profiles.find((entry) => entry.id === profile)?.cookies ?? [];
	const fromChrome = cookies.filter((cookie) => cookie.path === COOKIE_PATH);

	expect(
		SESSION.every((expected) =>
			fromChrome.some(
				(cookie) =>
					cookie.name === expected.name &&
					cookie.domain ===
						(expected.url ? "accounts.google.com" : expected.domain),
			),
		),
		`the profile has ${JSON.stringify(fromChrome)}`,
	);

	expect(chromeQuit, "Chrome still runs after the transfer");
	expect(
		!existsSync(chromeProfile),
		`Chrome's profile ${chromeProfile} is still there`,
	);

	// Google's storage, then Sign out.
	const googlePage = await pageAt(app, "https://www.google.com/");
	await googlePage.evaluate(`localStorage.setItem("pane-scenario", "1")`);

	await app.send("readCookies", { profile });

	const beforeSignOut = await app.waitFor(
		"the profile's cookies, before signing out",
		(state) => !!state.profiles.find((entry) => entry.id === profile)?.cookies,
		20_000,
	);

	const googleCookies = (
		beforeSignOut.profiles.find((entry) => entry.id === profile)?.cookies ?? []
	).filter((cookie) => isGoogleDomain(cookie.domain));

	const deletesBefore = trace().filter(
		(entry) => entry.name === "PaneBrowserContext.deleteCookie",
	).length;

	await app.send("googleSignOut", { profile });

	await app.waitFor(
		"signed out",
		(state) =>
			state.profiles.find((entry) => entry.id === profile)?.googleSignedIn ===
			false,
		20_000,
	);

	await app.send("readCookies", { profile });

	const signedOut = await app.waitFor(
		"the profile's cookies, signed out",
		(state) => !!state.profiles.find((entry) => entry.id === profile)?.cookies,
		20_000,
	);

	// Every Google cookie went; the reloaded page sets its anonymous ones
	// (NID and the like) again, never an account's.
	const deleted =
		trace().filter((entry) => entry.name === "PaneBrowserContext.deleteCookie")
			.length - deletesBefore;

	const left = (
		signedOut.profiles.find((entry) => entry.id === profile)?.cookies ?? []
	).filter((cookie) => isGoogleDomain(cookie.domain));

	expect(
		deleted === googleCookies.length,
		`signing out deleted ${deleted} of ${googleCookies.length} Google cookies`,
	);

	expect(
		!left.some(
			(cookie) => AUTH_COOKIES.has(cookie.name) || cookie.path === COOKIE_PATH,
		),
		`account cookies left after signing out: ${JSON.stringify(left)}`,
	);

	// The sign-out reloaded the Google page; its storage is gone.
	await Bun.sleep(2_000);
	const reloaded = await pageAt(app, "https://www.google.com/");
	const storage = await reloaded.evaluate<string | null>(
		`localStorage.getItem("pane-scenario")`,
	);

	expect(storage === null, `www.google.com's localStorage kept ${storage}`);

	// A second rejection, then the tab goes elsewhere: its sign-in ends.
	const second = await app.openTab(
		profile,
		`${accounts}/signin/rejected?continue=${encodeURIComponent("https://mail.google.com/")}`,
	);

	await app.waitFor(
		"a second transfer page",
		(state) => !!signInOf(state, second)?.chromeProfile,
		30_000,
	);

	const secondProfile =
		signInOf(await app.state(), second)?.chromeProfile ?? "";
	await app.send("select", { tab: second });
	await app.send("navigate", { input: context.fixtures.url("smoke.html") });

	const cancelled = await app.waitFor(
		"the second sign-in ended",
		(state) =>
			signInOf(state, second) === null &&
			(tabOf(state, second)?.url ?? "").endsWith("smoke.html"),
		20_000,
	);

	await Bun.sleep(3_000);

	expect(
		!existsSync(secondProfile),
		"the abandoned sign-in's Chrome profile is still there",
	);

	return {
		accepted: Object.fromEntries(
			Object.entries(accepted).map(([name, result]) => [
				name,
				{
					url: `${result.url.origin}${result.url.pathname}`,
					identifierField: result.identifier,
					title: result.title,
				},
			]),
		),
		fallback: {
			rejected,
			chromeOpened: chromePage,
			emptyTransfer: signInOf(empty, tab)?.error,
			imported: fromChrome,
			continuedTo: tabOf(transferred, tab)?.url,
			chromeQuit,
			signOut: {
				googleCookiesBefore: googleCookies.length,
				deleted,
				setAgainByTheReload: left.map((cookie) => cookie.name),
			},
			googleStorageAfterSignOut: storage,
			abandoned: {
				url: tabOf(cancelled, second)?.url,
				chromeProfileRemoved: !existsSync(secondProfile),
			},
		},
		trace: {
			cookiesSet: has("PaneBrowserContext.setCookie"),
			siteDataCleared: has("PaneBrowserContext.clearSiteData"),
		},
	};
};

export default google;
