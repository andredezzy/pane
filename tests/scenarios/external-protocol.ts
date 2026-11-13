import { bySystem } from "../lib/platform";
import { isRunningNamed } from "../lib/processes";
import { answerSheet, WindowWatcher, waitForSheet } from "../lib/surfaces";
import { clickAt, launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// The link, to an app of the system's own that Chrome asks about first
// (mailto: would not ask: Chrome opens it directly): Maps on the Mac (the
// fixture's maps:), Settings on Windows, which has no Maps since Windows 11
// dropped it, so Chrome asks nothing for maps: there.
const HANDLED = bySystem({
	mac: { href: "maps:?q=Pane", app: "Maps" },
	windows: { href: "ms-settings:", app: "SystemSettings" },
});

// A link another app handles asks first; Cancel opens nothing.
const externalProtocol: Scenario = async (context) => {
	const { app, has } = await launchTraced(context, "external-protocol");
	const watcher = new WindowWatcher(app).start();
	const profile = await app.createProfile("Protocols");
	const url = context.fixtures.url("external.html");
	const { page } = await app.openPage(profile, url);

	const box = await page.evaluate<{ x: number; y: number }>(
		`(() => { const link = document.getElementById('mail'); link.href = ${JSON.stringify(HANDLED.href)}; const r = link.getBoundingClientRect(); return { x: r.left + 5, y: r.top + 5 }; })()`,
	);

	await clickAt((method, params) => page.send(method, params), box.x, box.y);

	const appBefore = await isRunningNamed(HANDLED.app);

	const sheet = await waitForSheet(app, "externalProtocol");
	await answerSheet(app, "Cancel");
	await Bun.sleep(1_000);

	const appAfter = await isRunningNamed(HANDLED.app);

	expect(appAfter === appBefore, `${HANDLED.app} opened`);

	expect(sheet.title?.startsWith("Open “"), `sheet ${sheet.title}`);
	expect(!has("PaneExternalProtocolRequest.open"), "the app opened the link");

	await watcher.stop();
};

export default externalProtocol;
