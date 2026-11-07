import { launchPane } from "../lib/pane";
import {
	answerSheet,
	pageResult,
	WindowWatcher,
	waitForSheet,
} from "../lib/surfaces";
import { expect, type Scenario } from "../run";

// A site's permission request shows as the app's sheet; the answer reaches
// the page. Each of its answers: Don't Allow (notifications), Allow (window
// management), and Allow This Time (idle detection), which grants the
// request without keeping the permission, as Allow keeps it for the site's
// next page. Clipboard writes need no prompt (the Electron app's policy).
const permissionPrompt: Scenario = async (context) => {
	const app = await launchPane(context);
	const watcher = new WindowWatcher(app).start();
	const profile = await app.createProfile("Permissions");
	const url = context.fixtures.url("permissions.html");
	const { tab, page } = await app.openPage(profile, url);
	const evaluate = (expression: string) => page.evaluate(expression);

	await page.gesture("askNotifications()");

	const notifications = await waitForSheet(app, "permission");

	expect(
		notifications.message?.includes("Show notifications"),
		`sheet ${notifications.message}`,
	);

	await answerSheet(app, "Don’t Allow");
	const denied = await pageResult<string>(evaluate);

	await page.gesture("askWindowManagement()");

	await waitForSheet(app, "permission");
	await answerSheet(app, "Allow");
	const allowed = await pageResult<string>(evaluate);

	await page.gesture("askIdle()");

	const idleDetection = await waitForSheet(app, "permission");

	expect(
		idleDetection.buttons?.join("|") === "Allow|Allow This Time|Don’t Allow",
		`buttons ${idleDetection.buttons}`,
	);

	await answerSheet(app, "Allow This Time");
	const allowedOnce = await pageResult<string>(evaluate);

	const clipboardWrite = await page.evaluate<string>(
		"stateOf('clipboard-write')",
	);

	// Esc dismisses, as Chrome's prompt closed does: the page gets no fonts
	// (an empty list, not an error), and the site may ask again.
	await page.gesture("askFonts()");

	await waitForSheet(app, "permission");
	await app.send("activate");
	await app.send("key", { key: "\u001b" });
	const dismissed = await pageResult<string>(evaluate);

	const fontsAfterEscape = await page.evaluate<string>(
		"stateOf('local-fonts')",
	);

	expect(
		dismissed === "granted 0" && fontsAfterEscape === "prompt",
		`Esc gave ${dismissed}, then ${fontsAfterEscape}`,
	);

	// The site's next page: what was kept.
	await app.send("closeTab", { tab });
	const next = context.fixtures.url("permissions.html?next");
	const { page: nextPage } = await app.openPage(profile, next);

	const kept = {
		notifications: await nextPage.evaluate<string>("stateOf('notifications')"),
		windowManagement: await nextPage.evaluate<string>(
			"stateOf('window-management')",
		),
		idleDetection: await nextPage.evaluate<string>("stateOf('idle-detection')"),
	};

	expect(denied === "denied", `notifications: ${denied}`);
	expect(allowed.startsWith("granted"), `window management: ${allowed}`);
	expect(allowedOnce === "granted", `idle detection: ${allowedOnce}`);
	expect(clipboardWrite === "granted", `clipboard-write is ${clipboardWrite}`);

	expect(
		kept.notifications === "denied" &&
			kept.windowManagement === "granted" &&
			kept.idleDetection === "prompt",
		`kept for the next page: ${JSON.stringify(kept)}`,
	);

	await watcher.stop();
};

export default permissionPrompt;
