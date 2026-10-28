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
	const tab = await app.openTab(profile, url);
	const page = await app.page(tab, url);

	const box = await page.evaluate<{ x: number; y: number }>(
		"(() => { const r = document.getElementById('mail').getBoundingClientRect(); return { x: r.left + 5, y: r.top + 5 }; })()",
	);

	await clickAt((method, params) => page.send(method, params), box.x, box.y);

	const mapsBefore =
		(await Bun.$`pgrep -x Maps`.quiet().nothrow()).exitCode === 0;

	const sheet = await waitForSheet(app, "externalProtocol");
	const answer = await answerSheet(app, "Cancel");
	await Bun.sleep(1_000);

	const mapsAfter =
		(await Bun.$`pgrep -x Maps`.quiet().nothrow()).exitCode === 0;

	expect(mapsAfter === mapsBefore, "Maps opened");

	expect(sheet.title?.startsWith("Open “"), `sheet ${sheet.title}`);

	expect(
		has("PaneWebContentsDelegate.openExternalProtocol", (args) =>
			String(args.url).startsWith("maps:"),
		),
		"trace lacks openExternalProtocol",
	);

	expect(has("PaneExternalProtocolRequest.cancel"), "trace lacks the cancel");
	expect(!has("PaneExternalProtocolRequest.open"), "the app opened the link");

	const windows = await watcher.stop();

	expect(
		windows.others.length === 0,
		`Chrome windows appeared: ${JSON.stringify(windows.others)}`,
	);

	return { sheet, answer, windows };
};

export default externalProtocol;
