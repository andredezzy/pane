import {
	benchmarkedApps,
	conditions,
	inTurn,
	launchApp,
	now,
	stats,
} from "../lib/benchmark";
import type { Page } from "../lib/cdp";
import type { Scenario } from "../run";

// Page loads, the Electron app against the native one: the fixtures' local
// pages and a few real sites, each opened in the app's first tab (CDP's
// Page.navigate, as typing it in the address bar), read with the page's own
// Performance API: navigation start to the load event's end, first and
// largest contentful paint, and the first byte. Each run is a launch on new
// data, so the first pass loads with an empty cache; the second, the same
// pages again, with what the first one cached. Between pages the tab goes to
// about:blank, so no page unloads into the next one's time.

const RUNS = 3;

const SITES = [
	"https://en.wikipedia.org/wiki/Web_browser",
	"https://www.apple.com/",
	"https://github.com/",
];

const LOCAL = ["smoke.html", "find.html", "nav-a.html"];

type Load = {
	url: string;
	pass: "cold" | "warm";
	finalUrl: string;
	// Milliseconds from navigation start, by the page's clock.
	loadEventEnd: number;
	domContentLoaded: number;
	responseStart: number;
	// When the first contentful frame was on screen (the entry's startTime,
	// its presentationTime in both engines), and when the renderer painted
	// it (paintTime): the difference is the frame's way to the screen.
	firstContentfulPaint: number | null;
	firstContentfulPaintPainted: number | null;
	largestContentfulPaint: number | null;
	transferSize: number;
	// From Page.navigate to Page.loadEventFired, by the runner's clock.
	navigateToLoad: number;
};

// Read 2 s after the load event: largest contentful paint can come after it.
const TIMING = `new Promise((resolve) => {
	let largest = null;
	new PerformanceObserver((list) => {
		for (const entry of list.getEntries()) {
			largest = entry;
		}
	}).observe({ type: "largest-contentful-paint", buffered: true });
	setTimeout(() => {
		const navigation = performance.getEntriesByType("navigation")[0];
		const paint = performance.getEntriesByName("first-contentful-paint")[0];
		resolve({
			finalUrl: location.href,
			loadEventEnd: navigation.loadEventEnd,
			domContentLoaded: navigation.domContentLoadedEventEnd,
			responseStart: navigation.responseStart,
			firstContentfulPaint: paint ? paint.startTime : null,
			firstContentfulPaintPainted: paint?.paintTime ?? null,
			largestContentfulPaint: largest ? largest.startTime : null,
			transferSize: navigation.transferSize,
		});
	}, 2000);
})`;

async function navigate(page: Page, url: string): Promise<number> {
	const loaded = page.cdp.waitFor(
		"Page.loadEventFired",
		(message) => message.sessionId === page.sessionId,
		60_000,
	);

	loaded.catch(() => {});
	const sentAt = now();
	await page.send("Page.navigate", { url });
	await loaded;

	return now() - sentAt;
}

const benchmarkPages: Scenario = async (context) => {
	const started = conditions();
	const apps = await benchmarkedApps(context);
	const urls = [...LOCAL.map((path) => context.fixtures.url(path)), ...SITES];

	const loads: Record<string, Load[]> = Object.fromEntries(
		apps.map((app) => [app.kind, []]),
	);

	for (let run = 0; run < RUNS; run++) {
		for (const app of inTurn(apps, run)) {
			const running = await launchApp(context, app, {
				url: context.fixtures.url(`smoke.html?pages=${run}`),
			});

			for (const pass of ["cold", "warm"] as const) {
				for (const url of urls) {
					await navigate(running.page, "about:blank");
					const navigateToLoad = await navigate(running.page, url);

					const timing =
						await running.page.evaluate<
							Omit<Load, "url" | "pass" | "navigateToLoad">
						>(TIMING);

					loads[app.kind]?.push({ url, pass, ...timing, navigateToLoad });
				}
			}

			await running.quit();
			await Bun.sleep(2_000);
		}
	}

	const metrics = [
		"loadEventEnd",
		"firstContentfulPaint",
		"firstContentfulPaintPainted",
		"largestContentfulPaint",
		"responseStart",
	] as const;

	const summary = Object.fromEntries(
		Object.entries(loads).map(([kind, entries]) => [
			kind,
			Object.fromEntries(
				["cold", "warm"].map((pass) => [
					pass,
					Object.fromEntries(
						urls.map((url) => {
							const runs = entries.filter(
								(entry) => entry.url === url && entry.pass === pass,
							);

							return [
								url.startsWith(context.fixtures.origin)
									? url.slice(context.fixtures.origin.length + 1)
									: url,
								Object.fromEntries(
									metrics.map((metric) => {
										const values = runs
											.map((entry) => entry[metric])
											.filter((value): value is number => value !== null);

										return [metric, values.length > 0 ? stats(values) : null];
									}),
								),
							];
						}),
					),
				]),
			),
		]),
	);

	return {
		runs: RUNS,
		conditions: { started, ended: conditions() },
		summary,
		loads,
	};
};

export default benchmarkPages;
