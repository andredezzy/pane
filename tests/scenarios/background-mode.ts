import { readFileSync } from "node:fs";
import { join } from "node:path";
import { launchPane } from "../lib/pane";
import { ENGINE_FOLDER } from "../lib/platform";
import { waitForExit } from "../lib/processes";
import { trayIconsOf } from "../lib/windows";
import { expect, type Scenario } from "../run";

const PROBE = join(
	import.meta.dir,
	"..",
	"pages",
	"extensions",
	"background-permission",
);

// Chrome's background mode stays off (ADR 0010): an extension that asks the
// "background" permission put Chrome's own icon in Windows' notification area,
// whose menu opens Chrome's task manager, About, and a window per profile,
// outside Pane's window. Now none shows, the engine's Local State says the mode
// is off on every system, and Quit still ends Pane.
const backgroundMode: Scenario = async (context) => {
	const app = await launchPane(context, {
		args: [`--load-extension=${PROBE}`],
	});

	const profile = await app.createProfile("Background");

	await app.openPage(
		profile,
		context.fixtures.url("smoke.html?background-mode"),
	);

	// Chrome's manager answers once the profile's extensions have loaded.
	await Bun.sleep(5_000);

	const icons = await trayIconsOf(app.browser.pid);

	expect(
		icons.length === 0,
		`Chrome's tray holds icons ${JSON.stringify(icons)} in the notification area`,
	);

	await app.quit();

	expect(await waitForExit(app.browser.pid, 30_000), "Pane didn't end at Quit");

	// Local State as the engine left it.
	const localState = JSON.parse(
		readFileSync(join(app.dataDir, ENGINE_FOLDER, "Local State"), "utf8"),
	) as { background_mode?: { enabled?: boolean } };

	expect(
		localState.background_mode?.enabled === false,
		`Chrome's background mode in Local State: ${JSON.stringify(localState.background_mode)}`,
	);

	return { icons, backgroundMode: localState.background_mode };
};

export default backgroundMode;
