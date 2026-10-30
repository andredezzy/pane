import {
	benchmarkedApps,
	conditions,
	inTurn,
	launchApp,
	stats,
} from "../lib/benchmark";
import type { Page } from "../lib/cdp";
import type { Scenario } from "../run";

// Browser benchmarks, the Electron app against the native one: Speedometer
// 3.1 and JetStream 3.0, from browserbench.org, in the app's first tab, in
// its window on screen. Each is started as its Start button starts it, and
// read once it is done. Both apps run with Chromium's throttling of
// occluded windows and background renderers off (lib/benchmark.ts), since
// the Mac is in use and another window can cover the benchmark's. Each run
// is a launch on new data.

const RUNS = 2;
const SPEEDOMETER = "https://browserbench.org/Speedometer3.1/";
const JETSTREAM = "https://browserbench.org/JetStream3.0/";
const SUITE_TIMEOUT_MS = 20 * 60_000;

async function navigate(page: Page, url: string): Promise<void> {
	const loaded = page.cdp.waitFor(
		"Page.loadEventFired",
		(message) => message.sessionId === page.sessionId,
		60_000,
	);

	loaded.catch(() => {});
	await page.send("Page.navigate", { url });
	await loaded;
}

// Polls `probe` in the page until it gives something, or fails after
// `timeoutMs`.
async function until<T>(
	page: Page,
	description: string,
	probe: string,
	timeoutMs: number,
	intervalMs: number,
): Promise<T> {
	const deadline = Date.now() + timeoutMs;

	while (Date.now() < deadline) {
		const value = await page.evaluate<T | null>(probe).catch(() => null);

		if (value !== null && value !== undefined) {
			return value;
		}

		await Bun.sleep(intervalMs);
	}

	throw new Error(`timed out waiting for ${description}`);
}

async function speedometer(page: Page) {
	await navigate(page, SPEEDOMETER);

	await until(
		page,
		"Speedometer",
		"globalThis.benchmarkClient ? true : null",
		30_000,
		250,
	);

	const startedAt = Date.now();
	await page.evaluate("globalThis.benchmarkClient.start(), true");

	const result = await until<{
		score: string;
		confidence: string;
		valid: boolean;
		iterations: number[];
	}>(
		page,
		"Speedometer's score",
		`(() => {
			const client = globalThis.benchmarkClient;
			if (!client || client._isRunning || !client._hasResults) {
				return null;
			}
			return {
				score: document.getElementById("result-number").textContent,
				confidence: document.getElementById("confidence-number").textContent,
				valid: document.getElementById("summary").className === "valid",
				iterations: (client._measuredValuesList ?? []).map((values) => values.score),
			};
		})()`,
		SUITE_TIMEOUT_MS,
		5_000,
	);

	return {
		url: SPEEDOMETER,
		score: Number(result.score),
		confidence: result.confidence,
		valid: result.valid,
		iterations: result.iterations,
		durationMs: Date.now() - startedAt,
	};
}

async function jetStream(page: Page) {
	await navigate(page, JETSTREAM);

	// It fetches every test's files before it can start.
	await until(
		page,
		"JetStream to load its tests",
		`(() => {
			if (globalThis.allIsGood === false) {
				throw new Error(document.getElementById("status")?.textContent ?? "JetStream failed");
			}
			return globalThis.JetStream?.isReady ? true : null;
		})()`,
		5 * 60_000,
		1_000,
	);

	// Given parameters, it would start by itself once ready: never twice.
	await Bun.sleep(1_000);
	const startedAt = Date.now();

	await page.evaluate(
		`(document.getElementById("status").textContent.includes("Running") || void JetStream.start(), true)`,
	);

	const result = await until<{ score: string; tests: Record<string, number> }>(
		page,
		"JetStream's score",
		`(() => {
			if (!globalThis.JetStream?.isDone) {
				return null;
			}
			const tests = {};
			for (const [name, value] of Object.entries(JetStream.simpleResultsObject())) {
				tests[name] = typeof value === "object" ? value.Score : value;
			}
			return { score: document.querySelector("#result-summary .score").textContent, tests };
		})()`,
		SUITE_TIMEOUT_MS,
		10_000,
	);

	return {
		url: JETSTREAM,
		score: Number(result.score),
		tests: result.tests,
		durationMs: Date.now() - startedAt,
	};
}

const benchmarkSuites: Scenario = async (context) => {
	const started = conditions();
	const apps = await benchmarkedApps(context);

	const runs: Record<
		string,
		{
			speedometer: Awaited<ReturnType<typeof speedometer>>;
			jetStream: Awaited<ReturnType<typeof jetStream>>;
			conditions: ReturnType<typeof conditions>;
		}[]
	> = Object.fromEntries(apps.map((app) => [app.kind, []]));

	for (let run = 0; run < RUNS; run++) {
		for (const app of inTurn(apps, run)) {
			const running = await launchApp(context, app, {
				url: context.fixtures.url(`smoke.html?suites=${run}`),
			});

			const before = conditions();
			const speedometerRun = await speedometer(running.page);
			const jetStreamRun = await jetStream(running.page);

			runs[app.kind]?.push({
				speedometer: speedometerRun,
				jetStream: jetStreamRun,
				conditions: before,
			});

			await running.quit();
			await Bun.sleep(5_000);
		}
	}

	return {
		runs: RUNS,
		conditions: { started, ended: conditions() },
		summary: Object.fromEntries(
			Object.entries(runs).map(([kind, entries]) => [
				kind,
				{
					speedometer: stats(entries.map((entry) => entry.speedometer.score)),
					jetStream: stats(entries.map((entry) => entry.jetStream.score)),
				},
			]),
		),
		suites: runs,
	};
};

export default benchmarkSuites;
