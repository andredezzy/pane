import { launchPane, sameTitle } from "../lib/pane";
import { WindowWatcher } from "../lib/surfaces";
import { expect, type Scenario } from "../run";

// Video picture-in-picture: the app hears of it, and the floating player is
// the one window besides the app's own (Chromium's PiP window, by design),
// titled in Chrome's words: "Picture in picture" on Windows.
const PLAYER = "Picture in Picture";

const pictureInPicture: Scenario = async (context) => {
	const app = await launchPane(context);

	const watcher = new WindowWatcher(app, (window) =>
		sameTitle(window.title, PLAYER),
	).start();

	const profile = await app.createProfile("PiP");
	const url = context.fixtures.url("pip.html");
	const { tab, page } = await app.openPage(profile, url);

	await page.evaluate(
		"new Promise((resolve) => video.readyState >= 1 ? resolve() : video.addEventListener('loadedmetadata', resolve))",
	);

	await page.gesture("video.requestPictureInPicture()", true);

	await app.waitFor("picture-in-picture", (state) =>
		state.pictureInPictureTabs.includes(tab),
	);

	await watcher.sample();

	await app.waitFor("the floating player", (state) =>
		state.nativeWindows.some(
			(window) => window.role === "other" && sameTitle(window.title, PLAYER),
		),
	);

	await page.evaluate("document.exitPictureInPicture()");

	await app.waitFor(
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

	await watcher.stop();
};

export default pictureInPicture;
