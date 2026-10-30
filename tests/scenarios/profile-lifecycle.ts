import { existsSync, mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { launchPane, tabOf } from "../lib/pane";
import { targets as targetsOf } from "../lib/targets";
import { clickAt, launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// A first launch shows the empty state; collapsing a profile unloads its
// pages but keeps its tabs; deleting a profile removes it and its data. A
// delete the engine never confirmed (a quit, a crash) is finished at the
// next launch. A profile whose page asks before it closes (beforeunload) is
// deleted without asking. A profile whose engine context will not open (an
// id with a slash, or too long to be a folder) shows its tab as the sad
// tab, and deleting it leaves nothing pending.
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

	writeFileSync(
		join(brokenDir, "profiles.json"),
		JSON.stringify({
			state: {
				profiles: [
					brokenProfile("broken/profile", "Broken", "broken-tab"),
					brokenProfile(longID, "Long", "long-tab"),
				],
			},
			version: 0,
		}),
	);

	const broken = await launchPane(context, { dataDir: brokenDir });
	await broken.send("select", { tab: "broken-tab" });

	const sadTab = await broken.waitFor(
		"the sad tab of a profile that will not open",
		(state) => state.crashedTabs.includes("broken-tab"),
	);

	const longSelected = Date.now();
	await broken.send("select", { tab: "long-tab" });

	await broken.waitFor(
		"the sad tab of a profile whose id is too long",
		(state) => state.crashedTabs.includes("long-tab"),
		5_000,
	);

	const longSadTabMs = Date.now() - longSelected;

	for (const id of ["broken/profile", longID]) {
		await broken.send("deleteProfile", { profile: id });
	}

	const brokenDeleted = await broken.waitFor(
		"nothing left pending",
		(state) =>
			state.profiles.length === 0 && state.pendingProfileDeletions === 0,
	);

	await broken.browser.close();

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

	return {
		empty: { profiles: empty.profiles.length, render: "empty-state.png" },
		unloaded: tabOf(unloaded, keepTab),
		reloaded: tabOf(reloaded, keepTab),
		deleted: deleted.profiles.map((profile) => profile.name),
		afterRelaunch: after.profiles.map((profile) => profile.name),
		profileDirRemoved: !existsSync(profileDir),
		unconfirmedDelete: orphan,
		contextThatWillNotOpen: {
			sadTab: sadTab.activeTabID === "broken-tab",
			longIDSadTabMs: longSadTabMs,
			pendingAfterDelete: brokenDeleted.pendingProfileDeletions,
		},
	};
};

export default profileLifecycle;
