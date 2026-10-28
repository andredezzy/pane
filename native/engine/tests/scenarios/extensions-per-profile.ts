import { existsSync } from "node:fs";
import { join } from "node:path";
import type { AppState, ExtensionState } from "../lib/pane";
import { waitForSheet } from "../lib/surfaces";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

const DARK_READER = "eimadpbcbfnmbkopoojfekhnkhdbieeh";

// Each profile has its own extensions: one installed in a profile lists,
// pins, enables, and removes there only, and lives in that profile's data
// directory. Another profile, opened beside it, has none until it installs
// its own, and keeps its own when the first one disables or removes it.
// Every control goes through the automation channel, so nothing on the screen
// is clicked.
const extensionsPerProfile: Scenario = async (context) => {
	const { app, trace } = await launchTraced(context, "extensions-per-profile");
	const first = await app.createProfile("First");
	const second = await app.createProfile("Second", "EMERALD");
	const firstURL = context.fixtures.url("light.html?first");
	const firstTab = await app.openTab(first, firstURL);
	await app.page(firstTab, firstURL);

	const item = (state: AppState, profile: string): ExtensionState | undefined =>
		(state.extensions[profile] ?? []).find((each) => each.id === DARK_READER);

	const listed = (state: AppState) => ({
		first: (state.extensions[first] ?? []).map((each) => each.id),
		second: (state.extensions[second] ?? []).map((each) => each.id),
	});

	const install = async (profile: string) => {
		await app.send("installExtension", { id: DARK_READER, profile });
		const sheet = await waitForSheet(app, "extensionInstall", 60_000);

		await app.send("clickSheetButton", {
			title: sheet.buttons?.[0] ?? "Add to Pane",
			texts: [],
		});

		return app.waitFor(
			`Dark Reader installed in ${profile}`,
			(state) => item(state, profile)?.enabled === true,
			120_000,
		);
	};

	const extensionDirectory = (profile: string) =>
		join(app.dataDir, "Engine", profile, "Extensions", DARK_READER);

	// Installed in the first profile.
	await install(first);

	// The second profile opens beside it, with no extension of its own.
	const secondURL = context.fixtures.url("light.html?second");
	const secondTab = await app.openTab(second, secondURL);
	await app.page(secondTab, secondURL);
	await app.send("select", { tab: secondTab });

	const opened = await app.waitFor(
		"the second profile open",
		(state) => state.activeProfileID === second && second in state.extensions,
	);

	// Long enough for anything the first install (or a migration) would
	// carry over to arrive.
	await Bun.sleep(5_000);
	const beside = await app.state();

	const onDisk = {
		first: existsSync(extensionDirectory(first)),
		second: existsSync(extensionDirectory(second)),
	};

	// One install call so far, the first profile's.
	const installs = trace().filter(
		(line) => line.name === "PaneBrowserContext.installExtension",
	).length;

	expect(
		item(beside, second) === undefined &&
			onDisk.first &&
			!onDisk.second &&
			installs === 1,
		`the second profile has the first one's extension: ${JSON.stringify({ listed: listed(beside), onDisk, installs })}`,
	);

	// The second profile installs its own; the first one's changes stay there.
	await install(second);

	await app.send("setExtensionEnabled", {
		id: DARK_READER,
		profile: first,
		enabled: false,
	});

	await app.send("setExtensionPinned", {
		id: DARK_READER,
		profile: first,
		pinned: false,
	});

	const changed = await app.waitFor(
		"the first profile's Dark Reader disabled and unpinned",
		(state) =>
			item(state, first)?.enabled === false &&
			item(state, first)?.pinned === false,
	);

	await Bun.sleep(1_000);
	const afterChange = await app.state();

	expect(
		item(afterChange, second)?.enabled === true &&
			item(afterChange, second)?.pinned === true,
		`the second profile followed the first: ${JSON.stringify(item(afterChange, second))}`,
	);

	await app.send("uninstallExtension", { id: DARK_READER, profile: first });

	const removed = await app.waitFor(
		"Dark Reader removed from the first profile",
		(state) => item(state, first) === undefined,
	);

	await Bun.sleep(1_000);
	const afterRemove = await app.state();

	expect(
		item(afterRemove, second)?.enabled === true,
		`the second profile lost its Dark Reader: ${JSON.stringify(listed(afterRemove))}`,
	);

	return {
		openedSecond: listed(opened),
		besideFirstInstall: { listed: listed(beside), onDisk, installs },
		afterFirstDisabledAndUnpinned: {
			first: item(changed, first),
			second: item(afterChange, second),
		},
		afterFirstRemoved: listed(removed),
		secondKept: listed(afterRemove),
	};
};

export default extensionsPerProfile;
