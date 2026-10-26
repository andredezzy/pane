import { existsSync, mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { launchPane, tabOf } from "../lib/pane";
import { expect, type Scenario } from "../run";

type Target = { type: string; url: string };

// A first launch shows the empty state; collapsing a profile unloads its
// pages but keeps its tabs; deleting a profile removes it and its data.
const profileLifecycle: Scenario = async (context) => {
	const dataDir = mkdtempSync(join(tmpdir(), "pane-lifecycle-"));
	const app = await launchPane(context, { dataDir });

	const empty = await app.state();

	expect(
		empty.profiles.length === 0 && empty.activeTabID === null,
		"a first launch should have no profiles",
	);

	await app.send("render", { path: join(context.out, "empty-state.png") });

	const keep = await app.createProfile("Keep", "EMERALD");
	const gone = await app.createProfile("Gone", "ROSE");
	const keepURL = context.fixtures.url("isolation.html?profile=keep");
	const goneURL = context.fixtures.url("isolation.html?profile=gone");
	const keepTab = await app.openTab(keep, keepURL);
	await app.page(keepTab, keepURL);
	const goneTab = await app.openTab(gone, goneURL);
	const gonePage = await app.page(goneTab, goneURL);
	await gonePage.evaluate("localStorage.pane = 'gone'");

	const targets = async () =>
		(
			await app.browser.cdp.send<{ targetInfos: Target[] }>("Target.getTargets")
		).targetInfos.filter((target) => target.type === "page");

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

	await app.browser.close();

	// Chrome removes a deleted profile's directory once nothing holds the
	// profile, or else in a low-priority cleanup at the next start.
	const relaunched = await launchPane(context, { dataDir });
	const after = await relaunched.state();
	const until = Date.now() + 20_000;

	while (existsSync(profileDir) && Date.now() < until) {
		await Bun.sleep(250);
	}

	await relaunched.browser.close();

	expect(
		after.profiles.map((profile) => profile.name).join(",") === "Keep",
		`profiles after relaunch: ${after.profiles.map((profile) => profile.name).join(",")}`,
	);

	expect(
		!existsSync(profileDir),
		`the deleted profile's data is still at ${profileDir}`,
	);

	return {
		empty: { profiles: empty.profiles.length, render: "empty-state.png" },
		unloaded: tabOf(unloaded, keepTab),
		reloaded: tabOf(reloaded, keepTab),
		deleted: deleted.profiles.map((profile) => profile.name),
		afterRelaunch: after.profiles.map((profile) => profile.name),
		profileDirRemoved: !existsSync(profileDir),
	};
};

export default profileLifecycle;
