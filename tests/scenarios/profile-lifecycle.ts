import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import {
	bringToFront,
	dataOpen,
	launchPane,
	profileFolder,
	tabOf,
} from "../lib/pane";
import { bySystem, ENGINE_FOLDER } from "../lib/platform";
import { poll, pollValue } from "../lib/poll";
import { waitForExit } from "../lib/processes";
import { targets as targetsOf } from "../lib/targets";
import { crashFolders, preferenceMacs } from "../lib/tools";
import { clickAt, launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// A first launch shows the empty state; collapsing a profile unloads its
// pages but keeps its tabs; deleting a profile, once its alert's Delete is
// chosen (Cancel keeps it), removes it and its data. A
// delete the engine never confirmed (a quit, a crash) is finished at the
// next launch. A profile whose page asks before it closes (beforeunload) is
// deleted without asking. A profile whose engine context will not open (an
// id with a slash, or too long to be a folder) shows its tab as the sad
// tab, and deleting it leaves nothing pending. A kill while a deleted profile's
// folder is going leaves its record, and the next launch finishes it. On
// Windows a deleted profile's preference MACs leave the registry with its
// folder, and a test copy keeps them under a key of its own, never the
// user's. No delete makes Chromium's crash folder (ADR 0115).
// Windows' system alert is Pane's own dialog (Paper 11.13 A); the Mac's is
// AppKit's sheet.
const PANES_OWN_ALERT = bySystem({ mac: false, windows: true });

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

	await app.render("empty-state.png", 0);

	const keep = await app.createProfile("Keep", "EMERALD");
	const gone = await app.createProfile("Gone", "ROSE");
	const keepURL = context.fixtures.url("isolation.html?profile=keep");
	const goneURL = context.fixtures.url("dialogs.html?profile=gone");
	const { tab: keepTab } = await app.openPage(keep, keepURL);
	const { page: gonePage } = await app.openPage(gone, goneURL);
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

	await poll(
		"the unloaded tab's page to go",
		async () => !(await targets()).some((target) => target.url === keepURL),
		10_000,
		200,
	);

	// Selecting the tab again loads it.
	await app.send("select", { tab: keepTab });
	await app.page(keepTab, keepURL);
	const reloaded = await app.state();
	expect(tabOf(reloaded, keepTab)?.loaded, "selecting the tab did not load it");

	// Delete: the profile, its tabs, its pages, and (by the next launch) its
	// engine data directory are gone.
	const profileDir = join(dataDir, ENGINE_FOLDER, gone);

	expect(
		existsSync(profileDir),
		"the profile has no engine directory to delete",
	);

	// Delete profile asks first, as the system asks: the Mac's alert sheet,
	// and on Windows Pane's own dialog over the window, as Exit Pane? is
	// (Paper 11.13 A), which a screen reader hears as a modal dialog named by
	// its title. Cancel keeps the profile; Delete deletes it.
	const askToDelete = async () => {
		await app.send("rowMenu", { profile: gone, choose: "Delete profile" });

		return (
			await app.waitFor(
				"the delete alert",
				(state) => (state.alert?.buttons.length ?? 0) > 0,
			)
		).alert;
	};

	const asked = await askToDelete();

	expect(
		asked?.title === "Delete “Gone”?" &&
			asked.message === "This action cannot be undone." &&
			JSON.stringify(asked.buttons) === JSON.stringify(["Cancel", "Delete"]),
		`the delete alert: ${JSON.stringify(asked)}`,
	);

	if (PANES_OWN_ALERT) {
		const read = (
			await app.send<
				Record<string, { role: string; name: string; modal: boolean }>
			>("accessibility", { prefix: "pane.alert" })
		).result;

		expect(
			read["pane.alert"]?.role === "dialog" &&
				read["pane.alert"].name === "Delete “Gone”?" &&
				read["pane.alert"].modal &&
				read["pane.alert.cancel"]?.name === "Cancel" &&
				read["pane.alert.action"]?.name === "Delete",
			`the alert as a screen reader hears it: ${JSON.stringify(read)}`,
		);

		await app.render("delete-alert.png", 300);
	}

	// Its keys, where the window takes them: Tab goes round Cancel and Delete
	// alone, and Esc keeps the profile, as Cancel does.
	if (
		PANES_OWN_ALERT &&
		(await bringToFront(context, app, "the alert's keys"))
	) {
		const focus: string[] = [];

		for (let step = 0; step < 3; step++) {
			await app.send("key", { key: "\t" });
			await Bun.sleep(300);
			focus.push((await app.state()).focus);
		}

		expect(
			JSON.stringify(focus) ===
				JSON.stringify([
					"pane.alert.cancel",
					"pane.alert.action",
					"pane.alert.cancel",
				]),
			`Tab in the alert went to ${JSON.stringify(focus)}`,
		);

		await app.send("key", { key: "\u001b" });
	} else {
		await app.send("clickModalButton", { title: "Cancel" });
	}

	const kept = await app.waitFor(
		"the alert gone, the profile kept",
		(state) =>
			!state.alert && state.profiles.some((profile) => profile.id === gone),
	);

	expect(
		kept.profiles.some((profile) => profile.id === gone),
		"Cancel deleted the profile",
	);

	await askToDelete();
	await app.send("clickModalButton", { title: "Delete" });

	const deleted = await app.waitFor(
		"Gone deleted",
		(state) =>
			!state.profiles.some((profile) => profile.id === gone) &&
			state.pendingProfileDeletions === 0,
	);

	// The deletion clears Chromium's crash list with the profile's history,
	// which made Chromium's crash folder, empty, outside Pane's data (W9a):
	// Pane keeps none (ADR 0115).
	await Bun.sleep(1_000);

	const crashFoldersMade = crashFolders({
		home: join(dataDir, "home"),
		dataDir,
	});

	expect(
		crashFoldersMade.length === 0,
		`Delete profile made Chromium's crash folder: ${JSON.stringify(crashFoldersMade)}`,
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
		!(gone in deleted.extensions),
		"the deleted profile's extensions are still listed",
	);

	await app.browser.close();

	// A delete the engine never confirmed: deletions.json still names it,
	// beside any the file names already: on Windows a deleted profile's folder
	// goes only once the engine has closed its files, and a quit before then
	// leaves it named for the next open (ADR 0121).
	const orphanDir = join(dataDir, ENGINE_FOLDER, "orphan");
	mkdirSync(join(orphanDir, "Network"), { recursive: true });
	const deletionsFile = join(dataDir, "deletions.json");

	const pending = existsSync(deletionsFile)
		? (JSON.parse(readFileSync(deletionsFile, "utf8")) as { state: string[] })
				.state
		: [];

	writeFileSync(
		deletionsFile,
		JSON.stringify({ state: [...pending, "orphan"], version: 0 }),
	);

	// Chrome removes a deleted profile's directory once nothing holds the
	// profile, or else in a low-priority cleanup at the next start. Pane
	// purges what deletions.json names once the data is open, the Mac's
	// volume attached (ADR 0113).
	const relaunched = await launchPane(context, { dataDir });

	const after = await relaunched.waitFor("the data open", dataOpen);

	await pollValue(
		() => existsSync(profileDir),
		(left) => !left,
		20_000,
		250,
	);

	await relaunched.browser.close();

	// What the profiles left outside their folders: on Windows Chromium's
	// MACs of their preferences, in the test copy's own registry key, never
	// the user's, the deleted profile's gone with its folder (ADR 0121).
	// Keep's, written at each quit, show the copy's key is the one written.
	const macs = await preferenceMacs();

	expect(
		macs === null ||
			(macs.testCopies.includes(keep) &&
				!macs.testCopies.includes(gone) &&
				!macs.testCopies.includes("orphan") &&
				!macs.users.includes(keep) &&
				!macs.users.includes(gone)),
		`the profiles' preference MACs in the registry: ${JSON.stringify({ keep, gone, macs })}`,
	);

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
	await killed.app.openPage(doomed, doomedURL);
	const doomedFolder = profileFolder(killedDir, doomed);
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

	const macsAfterKill = await preferenceMacs();

	expect(
		macsAfterKill === null ||
			(!macsAfterKill.testCopies.includes(doomed) &&
				!macsAfterKill.users.includes(doomed)),
		`the killed delete left the profile's preference MACs: ${JSON.stringify(macsAfterKill)}`,
	);

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
