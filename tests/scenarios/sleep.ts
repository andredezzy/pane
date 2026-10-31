import { type AppState, tabOf } from "../lib/pane";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// Sleep, Unload, Keep loaded, and Trim (CONTEXT.md), a minute a second
// (PANE_TIME_SCALE=60). Tabs sleep after 2 minutes in the background, hidden
// profiles unload after 4, and any cache is over budget (0 MB). Profile A is
// the selected one: a background tab sleeps on time, the selected one, one
// with DevTools open, and one playing sound (PaneWebContents.audible) stay.
// Profile K is Keep loaded and never sleeps. Profile
// B signed in, then was hidden: its tabs sleep, which unloads it, and its
// cache is trimmed; it is still signed in, its local storage kept and its
// Cache Storage gone.

const SCALE = 60;

function loaded(state: AppState, tab: string): boolean {
	return tabOf(state, tab)?.loaded ?? false;
}

function profileOf(state: AppState, id: string) {
	return state.profiles.find((profile) => profile.id === id);
}

const sleep: Scenario = async (context) => {
	const { app, trace } = await launchTraced(context, "sleep", {
		env: { PANE_TIME_SCALE: String(SCALE) },
	});

	await app.send("setSleepSettings", {
		tabSleepAfterMinutes: 2,
		profileUnloadAfterMinutes: 4,
		cacheBudgetMB: 0,
	});

	// B signs in and caches, then stays hidden.
	const b = await app.createProfile("Signed in", "VIOLET");
	const signIn = context.fixtures.url("sign-in");
	const b1 = await app.openTab(b, signIn);
	const signedIn = await app.page(b1, signIn);
	await signedIn.evaluate("window.ready");

	await app.send("readCacheSize", { profile: b });

	const measured = await app.waitFor(
		"B's cache size",
		(state) => profileOf(state, b)?.cacheSize != null,
	);

	const cacheBefore = Number(profileOf(measured, b)?.cacheSize);

	// K is Keep loaded.
	const k = await app.createProfile("Kept", "AMBER");
	await app.send("setKeepLoaded", { profile: k, keepLoaded: true });
	const k1 = await app.openTab(k, context.fixtures.url("nav-a.html?kept"));

	// A: a background tab, one with DevTools, and the selected one.
	const a = await app.createProfile("Active", "BLUE");

	const a1 = await app.openTab(
		a,
		context.fixtures.url("nav-a.html?background"),
	);

	// a3 opens selected, which starts a1's clock. Timed from a2, most of a
	// second later, the check passed or failed on where the ticks fell.
	const a3 = await app.openTab(a, context.fixtures.url("nav-a.html?devtools"));
	const backgrounded = Date.now();

	await app.waitFor(
		"a3 loaded",
		(state) => loaded(state, a3) && !tabOf(state, a3)?.loading,
	);

	await app.send("toggleDevTools");

	await app.waitFor("DevTools on a3", (state) =>
		state.devToolsTabs.includes(a3),
	);

	// A tone no one hears, which Chrome counts as sound.
	const audioURL = context.fixtures.url("audio.html");
	const a4 = await app.openTab(a, audioURL);
	const audio = await app.page(a4, audioURL);

	const playing = await audio.send<{ result: { value?: string } }>(
		"Runtime.evaluate",
		{ expression: "play()", userGesture: true, awaitPromise: true },
	);

	expect(
		playing.result.value === "running",
		`the audio did not start: ${JSON.stringify(playing)}`,
	);

	const a2 = await app.openTab(a, context.fixtures.url("nav-a.html?selected"));

	const slept = await app.waitFor(
		"a1 asleep",
		(state) => !loaded(state, a1),
		15_000,
	);

	const sleptAfter = (Date.now() - backgrounded) / 1000;

	// Two scaled minutes, plus up to a tick.
	expect(
		sleptAfter >= 120 / SCALE - 0.3 && sleptAfter <= 120 / SCALE + 2,
		`a1 slept after ${sleptAfter}s, not ~${120 / SCALE}s`,
	);

	// B's tab sleeps too, which unloads B, and B is trimmed.
	const unloaded = await app.waitFor(
		"B unloaded",
		(state) => !loaded(state, b1),
		20_000,
	);

	await app.waitFor(
		"B trimmed",
		() =>
			trace().some((entry) => entry.name === "PaneBrowserContext.trimCache"),
		10_000,
	);

	// Long enough for everything else to have slept, were it going to.
	await Bun.sleep((6 * 60 * 1000) / SCALE);
	const settled = await app.state();

	expect(loaded(settled, a2), "the selected tab slept");
	expect(loaded(settled, a3), "the tab with DevTools open slept");
	expect(loaded(settled, a4), "the tab playing sound slept");
	expect(loaded(settled, k1), "a Keep loaded profile's tab slept");

	expect(
		!loaded(settled, a1) && !loaded(settled, b1),
		"a sleeping tab woke by itself",
	);

	// Trim: the cache is gone, the identity is not.
	await app.send("readCacheSize", { profile: b });

	const trimmed = await app.waitFor(
		"B's cache size after the trim",
		(state) => profileOf(state, b)?.cacheSize != null,
	);

	const cacheAfter = Number(profileOf(trimmed, b)?.cacheSize);

	expect(
		cacheBefore > 256 * 1024 && cacheAfter < cacheBefore / 4,
		`B's cache went from ${cacheBefore} to ${cacheAfter} bytes`,
	);

	await app.send("readCookies", { profile: b });

	const cookies = await app.waitFor(
		"B's cookies",
		(state) => !!profileOf(state, b)?.cookies,
	);

	expect(
		profileOf(cookies, b)?.cookies?.some(
			(cookie) => cookie.name === "pane_session",
		) ?? false,
		`B lost its sign-in cookie: ${JSON.stringify(profileOf(cookies, b)?.cookies)}`,
	);

	// Selecting the sleeping tab brings its page back, still signed in.
	await app.send("select", { tab: b1 });
	const woken = await app.page(b1, signIn);

	const identity = await woken.evaluate<{
		cookie: string;
		storage: string | null;
		caches: string[];
	}>(
		`(async () => ({ cookie: document.cookie, storage: localStorage.getItem("pane-session"), caches: await caches.keys() }))()`,
	);

	// The page sets nothing on a return visit: what it sees survived.
	const entry = await woken.evaluate<boolean>(
		`(async () => !!(await (await caches.open("pane-trim")).match("cacheable?n=3")))()`,
	);

	expect(
		identity.cookie.includes("pane_session=signed-in"),
		`the woken page's cookies: ${identity.cookie}`,
	);

	expect(
		identity.storage === "kept",
		`the woken page's local storage: ${identity.storage}`,
	);

	expect(
		!identity.caches.includes("pane-trim") && !entry,
		`Cache Storage survived the trim: ${JSON.stringify(identity.caches)}`,
	);

	await app.send("select", { tab: a1 });

	const restored = await app.waitFor(
		"a1 back",
		(state) => loaded(state, a1) && !tabOf(state, a1)?.loading,
	);

	expect(
		tabOf(restored, a1)?.url === context.fixtures.url("nav-a.html?background"),
		`a1 came back at ${tabOf(restored, a1)?.url}`,
	);

	return {
		settings: {
			tabSleepAfterMinutes: 2,
			profileUnloadAfterMinutes: 4,
			cacheBudgetMB: 0,
			timeScale: SCALE,
		},
		tabSleep: {
			tab: a1,
			sleptAfterSeconds: sleptAfter,
			scaledMinutes: (sleptAfter * SCALE) / 60,
		},
		stayedLoaded: {
			selected: loaded(settled, a2),
			devTools: loaded(settled, a3),
			audible: loaded(settled, a4),
			keepLoaded: loaded(settled, k1),
		},
		unload: { profile: b, tabUnloaded: !loaded(unloaded, b1) },
		trim: {
			cacheBytesBefore: cacheBefore,
			cacheBytesAfter: cacheAfter,
			signInCookieKept: identity.cookie.includes("pane_session=signed-in"),
			localStorageKept: identity.storage,
			cacheStorageEntryAfterWake: entry,
			cacheStorageNames: identity.caches,
		},
		restored: { tab: a1, url: tabOf(restored, a1)?.url },
		slept: { a1: !loaded(slept, a1) },
	};
};

export default sleep;
