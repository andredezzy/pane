import { WindowWatcher } from "../lib/surfaces";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// Video picture-in-picture: the app hears of it, and the floating player is
// the one window besides the app's own (Chromium's PiP window, by design).
const pictureInPicture: Scenario = async (context) => {
	const { app, has } = await launchTraced(context, "picture-in-picture");
	const watcher = new WindowWatcher(app).start();
	const profile = await app.createProfile("PiP");
	const url = context.fixtures.url("pip.html");
	const tab = await app.openTab(profile, url);
	const page = await app.page(tab, url);

	await page.evaluate(
		"new Promise((resolve) => video.readyState >= 1 ? resolve() : video.addEventListener('loadedmetadata', resolve))",
	);

	await page.send("Runtime.evaluate", {
		expression: "video.requestPictureInPicture()",
		userGesture: true,
		awaitPromise: true,
	});

	const entered = await app.waitFor("picture-in-picture", (state) =>
		state.pictureInPictureTabs.includes(tab),
	);

	await watcher.sample("pip");
	const during = (await app.state()).nativeWindows;

	await page.evaluate("document.exitPictureInPicture()");

	const left = await app.waitFor(
		"the end of picture-in-picture",
		(state) => !state.pictureInPictureTabs.includes(tab),
	);

	const events = await page.evaluate<{ entered: boolean; left: boolean }>(
		"window.pip",
	);

	expect(
		events.entered && events.left,
		`page events ${JSON.stringify(events)}`,
	);

	expect(
		has(
			"PaneWebContentsDelegate.didChangePictureInPicture",
			(args) => args.active === true,
		),
		"trace lacks the start",
	);

	expect(
		has(
			"PaneWebContentsDelegate.didChangePictureInPicture",
			(args) => args.active === false,
		),
		"trace lacks the end",
	);

	const windows = await watcher.stop();

	const otherClasses = [
		...new Set(windows.others.map((window) => window.class)),
	];

	return {
		entered: entered.pictureInPictureTabs,
		left: left.pictureInPictureTabs,
		events,
		windowsDuringPictureInPicture: during,
		pictureInPictureWindowClasses: otherClasses,
		windows,
	};
};

export default pictureInPicture;
