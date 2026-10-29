import { existsSync, readFileSync } from "node:fs";
import { join } from "node:path";
import { launchPane } from "../lib/pane";
import { waitForExit } from "../lib/processes";
import { answerSheet, waitForSheet } from "../lib/surfaces";
import { expect, type Scenario } from "../run";

const DARK_READER = "eimadpbcbfnmbkopoojfekhnkhdbieeh";

// Chrome's update service, which its extension updater asks for the Web
// Store's extensions, naming each in a header.
const UPDATE_SERVICE = "update.googleapis.com/service/update2/json";

// Extensions update themselves, as in Chrome: there is no button for it.
// With one installed from the Web Store, its profile, once it opens on the
// next launch, asks the store's update service for newer versions on its
// own, as Chrome's updater does as a profile starts and every five hours
// after.
const extensionAutoUpdate: Scenario = async (context) => {
	const dataDir = context.tempDir("pane-auto-update-");
	const first = await launchPane(context, { dataDir });
	const profile = await first.createProfile("Updates");

	await first.send("installExtension", { id: DARK_READER, profile });
	const sheet = await waitForSheet(first, "extensionInstall", 60_000);
	await answerSheet(first, sheet.buttons?.[0] ?? "Add extension");

	await first.waitFor(
		"Dark Reader installed",
		(state) =>
			(state.extensions[profile] ?? []).some((item) => item.id === DARK_READER),
		120_000,
	);

	const firstPid = first.browser.pid;
	await first.send("quit").catch(() => undefined);

	expect(await waitForExit(firstPid, 30_000), "the first Pane did not quit");

	// The relaunch's network log, written in full once it quits.
	const netLog = join(context.tempDir("pane-net-log-"), "net-log.json");

	const relaunched = await launchPane(context, {
		dataDir,
		args: [`--log-net-log=${netLog}`],
	});

	// A profile's engine starts with its first tab.
	await relaunched.openTab(
		profile,
		context.fixtures.url("light.html?auto-update"),
	);

	await relaunched.waitFor(
		"Dark Reader loaded again",
		(state) =>
			(state.extensions[profile] ?? []).some((item) => item.id === DARK_READER),
		60_000,
	);

	// The check runs as soon as the browser is idle after the profile loads.
	await Bun.sleep(15_000);

	const relaunchedPid = relaunched.browser.pid;
	await relaunched.send("quit").catch(() => undefined);

	expect(
		await waitForExit(relaunchedPid, 30_000),
		"the relaunched Pane did not quit",
	);

	const log = existsSync(netLog) ? readFileSync(netLog, "utf8") : "";
	const asked = log.includes(UPDATE_SERVICE);

	const named = [...log.matchAll(/X-Goog-Update-AppId: ([a-p,]+)/gi)].flatMap(
		(match) => (match[1] ?? "").split(","),
	);

	expect(
		asked && named.includes(DARK_READER),
		`the relaunch asked ${UPDATE_SERVICE}: ${asked}, for ${named.join(", ") || "nothing"}`,
	);

	return { asked, named: [...new Set(named)] };
};

export default extensionAutoUpdate;
