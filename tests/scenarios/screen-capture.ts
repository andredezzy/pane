import { pageResult, WindowWatcher } from "../lib/surfaces";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// getDisplayMedia() and Cast are declined without Chrome's screen picker or
// Cast dialog, as in the Electron app.
const screenCapture: Scenario = async (context) => {
	const { app, has } = await launchTraced(context, "screen-capture");
	const watcher = new WindowWatcher(app).start();
	const profile = await app.createProfile("Capture");
	const url = context.fixtures.url("devices.html?capture");
	const { page } = await app.openPage(profile, url);
	const evaluate = (expression: string) => page.evaluate(expression);

	await page.gesture("askScreen()");

	const screen = await pageResult<string>(evaluate);
	await watcher.sample();

	await page.gesture("askCast()");

	const cast = await pageResult<string>(evaluate, 20_000);
	await watcher.sample();

	expect(screen === "screen: NotSupportedError", screen);
	expect(cast !== "cast: granted", cast);

	expect(
		has("policy.screenCaptureDeclined"),
		"trace lacks the declined capture",
	);

	await watcher.stop();
};

export default screenCapture;
