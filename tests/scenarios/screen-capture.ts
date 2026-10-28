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
	const tab = await app.openTab(profile, url);
	const page = await app.page(tab, url);
	const evaluate = (expression: string) => page.evaluate(expression);

	await page.send("Runtime.evaluate", {
		expression: "askScreen()",
		userGesture: true,
	});

	const screen = await pageResult<string>(evaluate);
	await watcher.sample("screen");

	await page.send("Runtime.evaluate", {
		expression: "askCast()",
		userGesture: true,
	});

	const cast = await pageResult<string>(evaluate, 20_000);
	await watcher.sample("cast");

	expect(screen === "screen: NotSupportedError", screen);
	expect(cast !== "cast: granted", cast);

	expect(
		has("policy.screenCaptureDeclined"),
		"trace lacks the declined capture",
	);

	const windows = await watcher.stop();

	expect(
		windows.others.length === 0,
		`Chrome windows appeared: ${JSON.stringify(windows.others)}`,
	);

	return { screen, cast, windows };
};

export default screenCapture;
