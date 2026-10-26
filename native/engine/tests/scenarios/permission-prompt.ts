import {
	answerSheet,
	pageResult,
	WindowWatcher,
	waitForSheet,
} from "../lib/surfaces";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// A site's permission request shows as the app's sheet; the answer reaches
// the page. Clipboard writes need no prompt (the Electron app's policy).
const permissionPrompt: Scenario = async (context) => {
	const { app, has } = await launchTraced(context, "permission-prompt");
	const watcher = new WindowWatcher(app).start();
	const profile = await app.createProfile("Permissions");
	const url = context.fixtures.url("permissions.html");
	const tab = await app.openTab(profile, url);
	const page = await app.page(tab, url);
	const evaluate = (expression: string) => page.evaluate(expression);

	await page.send("Runtime.evaluate", {
		expression: "askNotifications()",
		userGesture: true,
	});

	const notifications = await waitForSheet(app, "permission");

	expect(
		notifications.message?.includes("Show notifications"),
		`sheet ${notifications.message}`,
	);

	const deny = await answerSheet(app, "Don’t Allow");
	const denied = await pageResult<string>(evaluate);

	await page.send("Runtime.evaluate", {
		expression: "askWindowManagement()",
		userGesture: true,
	});

	const windowManagement = await waitForSheet(app, "permission");
	const allow = await answerSheet(app, "Allow");
	const allowed = await pageResult<string>(evaluate);

	const clipboardWrite = await page.evaluate<string>(
		"stateOf('clipboard-write')",
	);

	expect(denied === "denied", `notifications: ${denied}`);
	expect(allowed.startsWith("granted"), `window management: ${allowed}`);
	expect(clipboardWrite === "granted", `clipboard-write is ${clipboardWrite}`);

	expect(
		has("PaneWebContentsDelegate.requestPermission"),
		"trace lacks requestPermission",
	);

	expect(
		has("PanePermissionRequest.deny") && has("PanePermissionRequest.allow"),
		"trace lacks the answers",
	);

	const windows = await watcher.stop();

	expect(
		windows.others.length === 0,
		`Chrome windows appeared: ${JSON.stringify(windows.others)}`,
	);

	return {
		notifications,
		windowManagement,
		answers: [deny, allow],
		denied,
		allowed,
		clipboardWrite,
		windows,
	};
};

export default permissionPrompt;
