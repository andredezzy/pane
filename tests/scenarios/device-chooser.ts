import { pageResult, WindowWatcher } from "../lib/surfaces";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// USB, HID, serial, and Bluetooth pickers are declined without Chrome's chooser
// bubble, as in the Electron app; the page hears NotFoundError.
const deviceChooser: Scenario = async (context) => {
	const { app } = await launchTraced(context, "device-chooser");
	const watcher = new WindowWatcher(app).start();
	const profile = await app.createProfile("Devices");
	const url = context.fixtures.url("devices.html");
	const { page } = await app.openPage(profile, url);
	const answers: Record<string, string> = {};

	for (const ask of ["askUsb", "askHid", "askSerial", "askBluetooth"]) {
		await page.gesture(`${ask}()`);

		answers[ask] = await pageResult<string>((expression) =>
			page.evaluate(expression),
		);

		await watcher.sample();
	}

	expect(answers.askUsb === "usb: NotFoundError", `usb: ${answers.askUsb}`);
	expect(answers.askHid === "hid: 0 devices", `hid: ${answers.askHid}`);

	expect(
		answers.askSerial === "serial: NotFoundError",
		`serial: ${answers.askSerial}`,
	);

	expect(
		answers.askBluetooth === "bluetooth: NotFoundError",
		`bluetooth: ${answers.askBluetooth}`,
	);

	await watcher.stop();
};

export default deviceChooser;
