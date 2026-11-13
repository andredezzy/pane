import { isRunningNamed } from "../lib/processes";
import { answerSheet, WindowWatcher, waitForSheet } from "../lib/surfaces";
import { clickAt, launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// A link another app handles asks first; Cancel opens nothing. (mailto:
// would not ask: Chrome opens it directly, so the fixture uses maps:.)
const externalProtocol: Scenario = async (context) => {
	const { app, has } = await launchTraced(context, "external-protocol");
	const watcher = new WindowWatcher(app).start();
	const profile = await app.createProfile("Protocols");
	const url = context.fixtures.url("external.html");
	const { page } = await app.openPage(profile, url);

	const box = await page.evaluate<{ x: number; y: number }>(
		"(() => { const r = document.getElementById('mail').getBoundingClientRect(); return { x: r.left + 5, y: r.top + 5 }; })()",
	);

	await clickAt((method, params) => page.send(method, params), box.x, box.y);

	const mapsBefore = await isRunningNamed("Maps");

	const sheet = await waitForSheet(app, "externalProtocol");
	await answerSheet(app, "Cancel");
	await Bun.sleep(1_000);

	const mapsAfter = await isRunningNamed("Maps");

	expect(mapsAfter === mapsBefore, "Maps opened");

	expect(sheet.title?.startsWith("Open “"), `sheet ${sheet.title}`);
	expect(!has("PaneExternalProtocolRequest.open"), "the app opened the link");

	await watcher.stop();
};

export default externalProtocol;
