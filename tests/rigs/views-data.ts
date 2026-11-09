import { readFileSync, realpathSync } from "node:fs";
import { join } from "node:path";
import { type AppState, launchPane, type PaneApp, setUpPin } from "../lib/pane";
import { imagesAttachedIn, mountedAt, waitForExit } from "../lib/processes";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// Phase 2's model and data on the Views build (docs/plans/views-migration.md,
// C2 to C6): the model (BrowserModel) loaded from and saved to its files
// (ModelStore), the profiles opened through the vault, and the lock.
//
// - Two profiles with tabs, made through the channel, a theme changed, saved:
//   profiles.json is one sealed box, no profile, page, or title of it in the
//   clear; settings.json is plain.
// - Quit, and a relaunch on the same data: the same profiles, colors, tabs,
//   ids, addresses, and titles, in order; as the Swift app restores them, no
//   tab selected, every profile collapsed, no page made. The tab selected
//   before the quit, selected again, makes its page at its address.
// - A PIN set through the PIN screen (showPinScreen, pinKeys): the relaunch
//   is locked, with no profile in the model; a wrong PIN is refused and
//   counted; the right one opens the data, and the profiles come back.
// - Lock Pane while open (ADR 0116): the PIN screen at once, the profiles out
//   of the model, the volume detached behind it, every process's engine keys
//   dropped; the PIN brings back the tab that showed, its page made again.
// - Every quit detaches the volume. For the Views build:
//
//   bun tests/run.ts --binary <out>/Pane.app/Contents/MacOS/Pane --out <dir> \
//     tests/rigs/views-data.ts

const PIN = "2468";

type Saved = {
	id: string;
	name: string;
	color: string;
	tabs: { id: string; url: string; title: string }[];
}[];

const saved = (state: AppState): Saved =>
	state.profiles.map((profile) => ({
		id: profile.id,
		name: profile.name,
		color: profile.color,
		tabs: profile.tabs.map(({ id, url, title }) => ({ id, url, title })),
	}));

function json(path: string): Record<string, unknown> {
	return JSON.parse(readFileSync(path, "utf8")) as Record<string, unknown>;
}

// Quits through the channel: saved, closed, and the volume detached.
async function quit(app: PaneApp, dataDir: string) {
	const pid = app.browser.pid;
	const started = Date.now();
	await app.quit();

	expect(await waitForExit(pid, 30_000), "Pane did not quit");

	const after = {
		quitInMs: Date.now() - started,
		attachedImages: (await imagesAttachedIn(dataDir)).length,
		mounted: await mountedAt(join(dataDir, "Engine", "Volume")),
	};

	await app.browser.close();

	expect(
		after.attachedImages === 0 && !after.mounted,
		`the volume after the quit: ${JSON.stringify(after)}`,
	);

	return after;
}

async function poll<T>(
	what: string,
	look: () => Promise<T | undefined>,
	timeoutMs = 20_000,
): Promise<T> {
	const deadline = Date.now() + timeoutMs;

	while (Date.now() < deadline) {
		const found = await look();

		if (found !== undefined) {
			return found;
		}

		await Bun.sleep(100);
	}

	throw new Error(`timed out waiting for ${what}`);
}

const viewsData: Scenario = async (context) => {
	const dataDir = realpathSync(context.tempDir("pane-views-data-"));

	const url = (name: string) =>
		context.fixtures.url(`switcher.html?name=${encodeURIComponent(name)}`);

	// Two profiles with tabs, each page loaded before the next opens.
	const first = await launchPane(context, { dataDir });
	const work = await first.createProfile("Work", "ROSE");
	const personal = await first.createProfile("Personal", "EMERALD");
	const tabs: Record<string, string> = {};

	for (const [profile, names] of [
		[work, ["Mail", "Docs"]],
		[personal, ["News"]],
	] as const) {
		for (const name of names) {
			tabs[name] = (await first.openPage(profile, url(name))).tab;
		}
	}

	await first.send("select", { tab: tabs.Mail });
	await first.send("theme", { theme: "dark" });

	const before = await first.waitFor(
		"Mail selected, every tab titled and loaded",
		(state) =>
			state.activeTabID === tabs.Mail &&
			state.activeProfileID === work &&
			state.profiles
				.flatMap((profile) => profile.tabs)
				.every((tab) => tab.title !== "" && !tab.loading),
	);

	await first.send("save");

	// profiles.json is one sealed box; nothing of the profiles reads in it.
	const profilesText = readFileSync(join(dataDir, "profiles.json"), "utf8");
	const profilesFile = JSON.parse(profilesText) as Record<string, unknown>;
	const plain = ["Work", "Personal", "Mail", "News", "switcher.html", work];
	const inTheClear = plain.filter((text) => profilesText.includes(text));

	const settings = json(join(dataDir, "settings.json")) as {
		state?: { settings?: { theme?: string } };
	};

	const files = {
		profilesKeys: Object.keys(profilesFile),
		inTheClear,
		settingsTheme: settings.state?.settings?.theme,
	};

	expect(
		JSON.stringify(files.profilesKeys) === '["box"]' &&
			typeof profilesFile.box === "string" &&
			inTheClear.length === 0 &&
			files.settingsTheme === "dark",
		`the files: ${JSON.stringify(files)}`,
	);

	const firstQuit = await quit(first, dataDir);

	// The same profiles and tabs, as the Swift app restores them.
	const second = await launchPane(context, { dataDir });
	const restored = await second.state();

	const restoredShape = {
		same: JSON.stringify(saved(restored)) === JSON.stringify(saved(before)),
		activeTabID: restored.activeTabID,
		expanded: restored.profiles.map((profile) => profile.expanded),
		loaded: restored.profiles.flatMap((profile) =>
			profile.tabs.filter((tab) => tab.loaded).map((tab) => tab.id),
		),
		theme: restored.theme,
	};

	expect(
		restoredShape.same &&
			restoredShape.activeTabID === null &&
			restoredShape.expanded.every((expanded) => !expanded) &&
			restoredShape.loaded.length === 0 &&
			restoredShape.theme === "dark",
		`the relaunch: ${JSON.stringify({ restoredShape, before: saved(before), after: saved(restored) })}`,
	);

	// The tab selected before the quit: its page made at its address.
	await second.send("select", { tab: tabs.Mail });
	await second.page(tabs.Mail as string, url("Mail"));

	const reselected = await second.waitFor(
		"Mail's page",
		(state) =>
			state.activeTabID === tabs.Mail &&
			state.activeProfileID === work &&
			state.profiles.find((profile) => profile.id === work)?.expanded === true,
	);

	expect(
		reselected.profiles
			.flatMap((profile) => profile.tabs)
			.filter((tab) => tab.loaded)
			.map((tab) => tab.id)
			.join() === tabs.Mail,
		"only the tab selected has a page",
	);

	// A PIN, set as Settings sets it.
	await setUpPin(second, PIN);

	const security = json(join(dataDir, "security.json")) as {
		state: { key?: { pin?: unknown }; pin?: { length?: number } };
	};

	expect(
		security.state.pin?.length === PIN.length &&
			security.state.key?.pin !== undefined &&
			security.state.key?.pin !== null,
		`security.json after the PIN: ${JSON.stringify({ pin: security.state.pin, keyHasPin: security.state.key?.pin !== null })}`,
	);

	const secondQuit = await quit(second, dataDir);

	// Locked at launch: nothing of the profiles is read until the PIN.
	const third = await launchTraced(context, "views-data", { dataDir });
	const app = third.app;
	const atLaunch = await app.state();

	expect(
		atLaunch.security.locked &&
			atLaunch.security.pinLength === PIN.length &&
			atLaunch.security.screen?.mode === "unlock" &&
			atLaunch.profiles.length === 0 &&
			(await imagesAttachedIn(dataDir)).length <= 1 &&
			!(await mountedAt(join(dataDir, "Engine", "Volume"))),
		`at launch: ${JSON.stringify(atLaunch.security)} with ${atLaunch.profiles.length} profiles`,
	);

	// A wrong PIN: refused, and counted, the profiles still out.
	await app.send("pinKeys", { keys: "1357" });

	const refused = await app.waitFor(
		"the wrong PIN counted",
		(state) =>
			state.security.failedAttempts === 1 &&
			state.security.screen?.busy === false,
		15_000,
	);

	expect(
		refused.security.locked && refused.profiles.length === 0,
		`after a wrong PIN: ${JSON.stringify(refused.security)}`,
	);

	// The right PIN: the data opens and the profiles come back.
	const unlockStarted = Date.now();

	// The shake ends before the entry takes keys again.
	await app.waitFor(
		"the entry again",
		(state) => state.security.screen?.entered === 0,
		5_000,
	);

	await app.send("pinKeys", { keys: PIN });

	const unlocked = await app.waitFor(
		"unlocked, the data open",
		(state) =>
			!state.security.locked &&
			state.security.data.phase === "open" &&
			state.security.data.attached,
		30_000,
	);

	const unlockMs = Date.now() - unlockStarted;

	expect(
		JSON.stringify(saved(unlocked)) === JSON.stringify(saved(before)) &&
			unlocked.security.failedAttempts === 0,
		`after the PIN: ${JSON.stringify({ security: unlocked.security, profiles: saved(unlocked) })}`,
	);

	// Lock Pane while a tab shows: the PIN screen at once, the profiles out of
	// the model, the volume detached behind it.
	await app.send("select", { tab: tabs.News });
	await app.page(tabs.News as string, url("News"));

	const lockStarted = Date.now();
	const { state: lockedAtOnce } = await app.send("lockNow");

	expect(
		lockedAtOnce.security.locked &&
			lockedAtOnce.security.screen?.mode === "unlock" &&
			lockedAtOnce.profiles.length === 0 &&
			lockedAtOnce.activeTabID === null,
		`Lock Pane's reply: ${JSON.stringify({ security: lockedAtOnce.security, profiles: lockedAtOnce.profiles.length })}`,
	);

	const detachedInMs = await poll(
		"the volume detached at the lock",
		async () =>
			(await imagesAttachedIn(dataDir)).length === 0 &&
			!(await mountedAt(join(dataDir, "Engine", "Volume")))
				? Date.now() - lockStarted
				: undefined,
	);

	const dropped = await third.waitForEntry(
		"PaneBrowserApplication.engineKeysDropped",
	);

	expect(
		dropped.args.browser === 0 &&
			dropped.args.network === 0 &&
			dropped.args.storage === 0,
		`the keys left after the lock: ${JSON.stringify(dropped.args)}`,
	);

	// The PIN brings back the tab that showed.
	await app.send("pinKeys", { keys: PIN });

	const resumed = await app.waitFor(
		"the tab that showed, back",
		(state) =>
			!state.security.locked &&
			state.security.data.phase === "open" &&
			state.activeTabID === tabs.News &&
			state.profiles.some((profile) =>
				profile.tabs.some(
					(tab) => tab.id === tabs.News && tab.loaded && !tab.loading,
				),
			),
		30_000,
	);

	expect(
		JSON.stringify(saved(resumed)) === JSON.stringify(saved(before)),
		`the profiles after the Lock: ${JSON.stringify(saved(resumed))}`,
	);

	const thirdQuit = await quit(app, dataDir);

	return {
		files,
		firstQuit,
		restored: restoredShape,
		secondQuit,
		pin: {
			lockedAtLaunch: atLaunch.security.locked,
			wrongPinCounted: refused.security.failedAttempts,
			unlockMs,
		},
		lock: { detachedInMs, keysLeft: dropped.args },
		thirdQuit,
	};
};

export default viewsData;
