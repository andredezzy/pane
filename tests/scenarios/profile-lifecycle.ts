import { existsSync, mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { sealProfiles } from "../lib/engine-key";
import { launchPane, tabOf } from "../lib/pane";
import { poll } from "../lib/poll";
import { waitForExit } from "../lib/processes";
import { targets as targetsOf } from "../lib/targets";
import { clickAt, launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// A first launch shows the empty state; collapsing a profile unloads its
// pages but keeps its tabs; deleting a profile removes it and its data. A
// delete the engine never confirmed (a quit, a crash) is finished at the
// next launch. A profile whose page asks before it closes (beforeunload) is
// deleted without asking. A profile whose engine context will not open (an
// id with a slash, or too long to be a folder) shows its tab as the sad
// tab, and deleting it leaves nothing pending. A kill while a deleted profile's
// folder is going leaves its record, and the next launch finishes it.
const profileLifecycle: Scenario = async (context) => {
	const dataDir = context.tempDir("pane-lifecycle-");

	const { app, has } = await launchTraced(context, "profile-lifecycle", {
		dataDir,
	});

	const empty = await app.state();

	expect(
		empty.profiles.length === 0 && empty.activeTabID === null,
		"a first launch should have no profiles",
	);

	await app.send("render", { path: join(context.out, "empty-state.png") });

	const keep = await app.createProfile("Keep", "EMERALD");
	const gone = await app.createProfile("Gone", "ROSE");
	const keepURL = context.fixtures.url("isolation.html?profile=keep");
	const goneURL = context.fixtures.url("dialogs.html?profile=gone");
	const keepTab = await app.openTab(keep, keepURL);
	await app.page(keepTab, keepURL);
	const goneTab = await app.openTab(gone, goneURL);
	const gonePage = await app.page(goneTab, goneURL);
	await gonePage.evaluate("localStorage.pane = 'gone'");
	// A page that asks before it goes, once the user touched it.
	await gonePage.evaluate("armBeforeUnload()");
	await clickAt((method, params) => gonePage.send(method, params), 20, 20);

	const targets = async () =>
		(await targetsOf(app.browser.cdp)).filter(
			(target) => target.type === "page",
		);

	// Collapse (Unload): the tab stays, its page goes.
	await app.send("unloadProfile", { profile: keep });

	const unloaded = await app.waitFor(
		"Keep unloaded",
		(state) => tabOf(state, keepTab)?.loaded === false,
	);

	expect(
		tabOf(unloaded, keepTab)?.url === keepURL,
		"the unloaded tab lost its URL",
	);

	expect(
		!unloaded.profiles.find((profile) => profile.id === keep)?.expanded,
		"Keep is still expanded",
	);

	const deadline = Date.now() + 10_000;

	while (
		(await targets()).some((target) => target.url === keepURL) &&
		Date.now() < deadline
	) {
		await Bun.sleep(200);
	}

	expect(
		!(await targets()).some((target) => target.url === keepURL),
		"the unloaded tab's page still exists",
	);

	// Selecting the tab again loads it.
	await app.send("select", { tab: keepTab });
	await app.page(keepTab, keepURL);
	const reloaded = await app.state();
	expect(tabOf(reloaded, keepTab)?.loaded, "selecting the tab did not load it");

	// Delete: the profile, its tabs, its pages, and (by the next launch) its
	// engine data directory are gone.
	const profileDir = join(dataDir, "Engine", gone);

	expect(
		existsSync(profileDir),
		"the profile has no engine directory to delete",
	);

	await app.send("deleteProfile", { profile: gone });

	const deleted = await app.waitFor(
		"Gone deleted",
		(state) =>
			!state.profiles.some((profile) => profile.id === gone) &&
			state.pendingProfileDeletions === 0,
	);

	expect(
		!(await targets()).some((target) => target.url === goneURL),
		"the deleted profile's page still exists",
	);

	expect(
		!has(
			"PaneWebContentsDelegate.runJavaScriptDialog",
			(args) => args.type === "beforeunload",
		) && deleted.sheet === null,
		"deleting the profile asked its page (beforeunload)",
	);

	expect(
		has(
			"PaneBrowserApplication.didDeleteContext",
			(args) => args.deleted === true,
		),
		"the engine did not delete the profile",
	);

	expect(
		!(gone in deleted.extensions),
		"the deleted profile's extensions are still listed",
	);

	await app.browser.close();

	// A delete the engine never confirmed: deletions.json still names it.
	const orphanDir = join(dataDir, "Engine", "orphan");
	mkdirSync(join(orphanDir, "Network"), { recursive: true });

	writeFileSync(
		join(dataDir, "deletions.json"),
		JSON.stringify({ state: ["orphan"], version: 0 }),
	);

	// Chrome removes a deleted profile's directory once nothing holds the
	// profile, or else in a low-priority cleanup at the next start. Pane
	// purges what deletions.json names once the data volume is attached
	// (ADR 0113).
	const relaunched = await launchPane(context, { dataDir });

	const after = await relaunched.waitFor(
		"the data volume attached",
		(state) => state.security.data.attached,
	);

	const until = Date.now() + 20_000;

	while (existsSync(profileDir) && Date.now() < until) {
		await Bun.sleep(250);
	}

	await relaunched.browser.close();

	const orphan = {
		data: existsSync(orphanDir),
		listed: existsSync(join(dataDir, "deletions.json")),
	};

	// Profiles whose context will not open: the engine refuses an id that
	// is not one folder of its data, or one too long to be a folder name
	// (Chrome never finished loading one, and the open never answered).
	const brokenDir = context.tempDir("pane-lifecycle-broken-");
	const longID = "long".repeat(75);

	const brokenProfile = (id: string, name: string, tab: string) => ({
		id,
		name,
		color: "ROSE",
		group: null,
		fingerprint: null,
		proxy: null,
		keepLoaded: false,
		savesHistory: true,
		historyClearOwed: false,
		tabs: [
			{
				id: tab,
				url: context.fixtures.url(`smoke.html?${tab}`),
				title: "",
				favicon: "",
			},
		],
		createdAt: "2026-10-27T00:00:00.000Z",
		updatedAt: "2026-10-27T00:00:00.000Z",
	});

	// A first launch leaves the key they are sealed under.
	await (await launchPane(context, { dataDir: brokenDir })).browser.close();

	sealProfiles(brokenDir, [
		brokenProfile("broken/profile", "Broken", "broken-tab"),
		brokenProfile(longID, "Long", "long-tab"),
	]);

	const broken = await launchPane(context, { dataDir: brokenDir });
	await broken.send("select", { tab: "broken-tab" });

	await broken.waitFor("the sad tab of a profile that will not open", (state) =>
		state.crashedTabs.includes("broken-tab"),
	);

	await broken.send("select", { tab: "long-tab" });

	await broken.waitFor(
		"the sad tab of a profile whose id is too long",
		(state) => state.crashedTabs.includes("long-tab"),
		5_000,
	);

	for (const id of ["broken/profile", longID]) {
		await broken.send("deleteProfile", { profile: id });
	}

	await broken.waitFor(
		"nothing left pending",
		(state) =>
			state.profiles.length === 0 && state.pendingProfileDeletions === 0,
	);

	await broken.browser.close();

	// Killed once the engine confirmed a delete, with the folder still going:
	// thousands of files, so it takes a moment.
	const killedDir = context.tempDir("pane-lifecycle-killed-");

	const killed = await launchTraced(context, "profile-lifecycle-killed", {
		dataDir: killedDir,
	});

	const doomed = await killed.app.createProfile("Doomed", "ROSE");
	const doomedURL = context.fixtures.url("nav-a.html?doomed");
	await killed.app.page(await killed.app.openTab(doomed, doomedURL), doomedURL);
	const doomedFolder = join(killedDir, "Engine", "Volume", doomed);
	const filler = join(doomedFolder, "filler");
	mkdirSync(filler);

	for (let file = 0; file < 40_000; file++) {
		writeFileSync(join(filler, String(file)), "x");
	}

	await killed.app.send("deleteProfile", { profile: doomed });

	await killed.waitForEntry(
		"PaneBrowserApplication.didDeleteContext",
		(args) => args.deleted === true,
	);

	process.kill(killed.app.browser.pid, "SIGKILL");
	await waitForExit(killed.app.browser.pid);
	// The watchdog's detach, before the next launch attaches.
	await Bun.sleep(1_500);

	const afterKill = await launchPane(context, { dataDir: killedDir });

	const purged = await poll(
		"the killed delete finished",
		async () =>
			!existsSync(doomedFolder) &&
			(await afterKill.state()).pendingProfileDeletions === 0,
		30_000,
	).then(
		() => true,
		() => false,
	);

	await afterKill.browser.close();

	expect(
		purged,
		"a kill during a delete left the profile's folder, with no record of it",
	);

	expect(
		after.profiles.map((profile) => profile.name).join(",") === "Keep",
		`profiles after relaunch: ${after.profiles.map((profile) => profile.name).join(",")}`,
	);

	expect(
		!existsSync(profileDir),
		`the deleted profile's data is still at ${profileDir}`,
	);

	expect(
		!orphan.data && !orphan.listed,
		`an unconfirmed delete was not finished: ${JSON.stringify(orphan)}`,
	);
};

export default profileLifecycle;
