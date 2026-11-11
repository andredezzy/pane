import { stats } from "../lib/benchmark";
import { canDriveUI, clickButton, mainWindowOf } from "../lib/cua";
import { launchPane } from "../lib/pane";
import { startHttpProxy } from "../lib/proxies";
import { signpostIntervals } from "../lib/signposts";
import { expect, type Scenario, type ScenarioContext } from "../run";

// How long each signposted interaction (Log.interaction) takes, from the
// input to the frame Core Animation commits for it, read from the unified
// log as the app runs: selecting a tab among five loaded ones, a new tab
// from the File menu, focusing the address field, a click into it (the
// Views build's), opening the profile sheet (the first time draws the
// platforms' logos), and choosing a platform. The Views build also times the
// tab switcher's first frame, a tab without a page woken to the frame that
// shows its page's first paint (its profile unloaded, then the tab
// selected), and the profile sheet's Test connection to the frame showing
// its answer, through the run's own proxy. It times; it does not judge, so a
// change is measured before and after.

const ROUNDS = 8;

const CREDENTIALS = { username: "pane", password: "proxy-secret" };

// The app driven through each interaction, round after round; its process.
async function drive(context: ScenarioContext): Promise<number> {
	const driveUI = await canDriveUI();
	const fixturePort = Number(new URL(context.fixtures.origin).port);
	const proxy = await startHttpProxy(fixturePort, CREDENTIALS);

	try {
		return await driveWith(context, driveUI, proxy.port);
	} finally {
		proxy.stop();
	}
}

async function driveWith(
	context: ScenarioContext,
	driveUI: boolean,
	proxyPort: number,
): Promise<number> {
	const app = await launchPane(context, {
		env: { PANE_PROXY_TEST_URL: "http://check.pane.test/ip" },
	});

	const profile = await app.createProfile("Timing");
	const tabs: string[] = [];

	for (const name of ["Mail", "Docs", "Calendar", "News", "Music"]) {
		const url = context.fixtures.url(`switcher.html?name=${name}`);
		const { tab } = await app.openPage(profile, url);
		tabs.push(tab);
	}

	await Bun.sleep(1_000);

	for (let round = 0; round < ROUNDS; round++) {
		for (const tab of tabs) {
			await app.send("select", { tab });
			await Bun.sleep(150);
		}
	}

	for (let i = 0; i < ROUNDS * 2; i++) {
		await app.send("menu", { path: ["File", "New Tab"] });
		await Bun.sleep(250);
		await app.send("select", { tab: tabs[i % tabs.length] });
		await Bun.sleep(150);
		await app.send("menu", { path: ["Navigate", "Focus Address Bar"] });
		await Bun.sleep(150);
	}

	// A real click into the address, a tab's page having the keys before it
	// (the Views build's toolbar names it; the Swift's timed no click).
	const address = await app
		.frames("pane.toolbar.address")
		.catch(() => ({}) as Record<string, number[]>);

	if (address["pane.toolbar.address"]) {
		for (let i = 0; i < ROUNDS * 2; i++) {
			await app.send("select", { tab: tabs[i % tabs.length] });
			await Bun.sleep(150);
			await app.click("pane.toolbar.address");
			await Bun.sleep(150);
		}
	}

	// The tab switcher, opened and closed (the Views build's channel).
	const switcher = await app
		.send("switcher", { action: "next" })
		.then(() => app.send("switcher", { action: "cancel" }))
		.then(
			() => true,
			() => false,
		);

	if (switcher) {
		for (let i = 0; i < ROUNDS * 2; i++) {
			await Bun.sleep(150);
			await app.send("switcher", { action: "next" });
			await Bun.sleep(150);
			await app.send("switcher", { action: "cancel" });
		}
	}

	// A tab woken: its profile unloaded, its page gone, then the tab chosen,
	// until its page has loaded again.
	const waking = await app.createProfile("Waking");

	const asleep = await app.openTab(
		waking,
		context.fixtures.url("switcher.html?name=Asleep"),
	);

	await app.waitFor("the tab to wake loaded", (state) =>
		state.profiles.some((profile) =>
			profile.tabs.some(
				(tab) => tab.id === asleep && tab.loaded && !tab.loading,
			),
		),
	);

	for (let i = 0; i < ROUNDS * 2; i++) {
		await app.send("unloadProfile", { profile: waking });
		await Bun.sleep(150);
		await app.send("select", { tab: asleep });

		await app.waitFor("the tab awake", (state) =>
			state.profiles.some((profile) =>
				profile.tabs.some(
					(tab) => tab.id === asleep && tab.loaded && !tab.loading,
				),
			),
		);

		await Bun.sleep(300);
	}

	for (let round = 0; round < ROUNDS; round++) {
		await app.send("openProfileSheet");
		await Bun.sleep(400);

		// A press on a tile: a real click where the Views build names it,
		// else where accessibility can reach it (the Swift's).
		const tile =
			round % 2 === 0
				? "pane.profile.platform.windows"
				: "pane.profile.platform.macos";

		const tiles = await app
			.frames(tile)
			.catch(() => ({}) as Record<string, number[]>);

		if (tiles[tile]) {
			await app.click(tile);
			await Bun.sleep(150);
		} else if (driveUI) {
			const pid = app.browser.pid;
			const window = await mainWindowOf(pid);
			await clickButton(pid, window, round % 2 === 0 ? "Windows" : "macOS");
			await Bun.sleep(150);
		}

		await app.send("closeProfileSheet");
		await Bun.sleep(400);
	}

	// Test connection pressed in a proxied profile's sheet, each answer
	// waited for (the Views build's, which names the button).
	const proxied = await app.createProfile("Proxied");

	await app.send("setProfileProxy", {
		profile: proxied,
		proxy: {
			proxyType: "HTTP",
			host: "127.0.0.1",
			port: proxyPort,
			...CREDENTIALS,
		},
	});

	await app.send("openProfileSheet", { profile: proxied });
	await Bun.sleep(400);

	const test = await app
		.frames("pane.profile.testProxy")
		.catch(() => ({}) as Record<string, number[]>);

	if (test["pane.profile.testProxy"]) {
		for (let round = 0; round < ROUNDS; round++) {
			await app.click("pane.profile.testProxy");
			await Bun.sleep(1_000);
		}
	}

	await app.send("closeProfileSheet");
	await Bun.sleep(400);

	return app.browser.pid;
}

const interactionTiming: Scenario = async (context) => {
	const durations = await signpostIntervals(() => drive(context));

	expect(
		[
			"Select tab",
			"New tab",
			"Focus address field",
			"Open profile sheet",
		].every((name) => (durations[name]?.length ?? 0) > 0),
		`the log held no intervals for ${JSON.stringify(Object.keys(durations))}`,
	);

	return Object.fromEntries(
		Object.entries(durations).map(([name, values]) => {
			const { median, min, max } = stats(values);
			const sorted = [...values].sort((a, b) => a - b);
			const p90 = sorted[Math.floor(sorted.length * 0.9)] ?? max;
			const round = (value: number) => Math.round(value * 10) / 10;

			return [
				name,
				{
					count: values.length,
					samplesMs: values.length <= 8 ? values.map(round) : undefined,
					medianMs: round(median),
					p90Ms: round(p90),
					minMs: round(min),
					maxMs: round(max),
				},
			];
		}),
	);
};

export default interactionTiming;
