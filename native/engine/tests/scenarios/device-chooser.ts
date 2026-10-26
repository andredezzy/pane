import { pageResult, WindowWatcher } from "../lib/surfaces";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// USB, HID, and serial pickers are declined without Chrome's chooser bubble,
// as in the Electron app; the page hears NotFoundError.
const deviceChooser: Scenario = async (context) => {
	const { app, has } = await launchTraced(context, "device-chooser");
	const watcher = new WindowWatcher(app).start();
	const profile = await app.createProfile("Devices");
	const url = context.fixtures.url("devices.html");
	const tab = await app.openTab(profile, url);
	const page = await app.page(tab, url);
	const answers: Record<string, string> = {};

	for (const ask of ["askUsb", "askHid", "askSerial"]) {
		await page.send("Runtime.evaluate", {
			expression: `${ask}()`,
			userGesture: true,
		});

		answers[ask] = await pageResult<string>((expression) =>
			page.evaluate(expression),
		);

		await watcher.sample(ask);
	}

	expect(answers.askUsb === "usb: NotFoundError", `usb: ${answers.askUsb}`);
	expect(answers.askHid === "hid: 0 devices", `hid: ${answers.askHid}`);

	expect(
		answers.askSerial === "serial: NotFoundError",
		`serial: ${answers.askSerial}`,
	);

	expect(
		has("policy.deviceChooserDeclined"),
		"trace lacks the declined chooser",
	);

	const windows = await watcher.stop();

	expect(
		windows.others.length === 0,
		`Chrome windows appeared: ${JSON.stringify(windows.others)}`,
	);

	return { answers, windows };
};

export default deviceChooser;
