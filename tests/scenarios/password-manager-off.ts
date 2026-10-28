import { tabOf } from "../lib/pane";
import { WindowWatcher } from "../lib/surfaces";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// Chrome's password manager and autofill stay off (Pane relies on extension
// password managers): signing in shows no "Save password?" bubble.
const passwordManagerOff: Scenario = async (context) => {
	const { app } = await launchTraced(context, "password-manager-off");
	const watcher = new WindowWatcher(app).start();
	const profile = await app.createProfile("Passwords");
	const url = context.fixtures.url("password.html");
	const tab = await app.openTab(profile, url);
	const page = await app.page(tab, url);

	await page.evaluate(
		"document.getElementById('username').value = 'pane'; document.getElementById('password').value = 'a-test-password'; document.getElementById('login').submit()",
	);

	await app.waitFor(
		"the posted page",
		(state) =>
			tabOf(state, tab)?.title === "Posted" && !tabOf(state, tab)?.loading,
	);

	await Bun.sleep(2_000);
	await watcher.sample("after sign-in");
	const state = await app.state();

	expect(
		state.sheet === null && state.pendingRequests === 0,
		"something asked after the sign-in",
	);

	const windows = await watcher.stop();

	expect(
		windows.others.length === 0,
		`Chrome windows appeared: ${JSON.stringify(windows.others)}`,
	);

	return { sheet: state.sheet, windows };
};

export default passwordManagerOff;
