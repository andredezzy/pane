import {
	answerSheet,
	pageResult,
	WindowWatcher,
	waitForSheet,
} from "../lib/surfaces";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// A site's permission request shows as the app's sheet; the answer reaches
// the page. Each of its answers: Don't Allow (notifications), Allow (window
// management), and Allow This Time (idle detection), which grants the
// request without keeping the permission, as Allow keeps it for the site's
// next page. Clipboard writes need no prompt (the Electron app's policy).
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

	await page.send("Runtime.evaluate", {
		expression: "askIdle()",
		userGesture: true,
	});

	const idleDetection = await waitForSheet(app, "permission");

	expect(
		idleDetection.buttons?.join("|") === "Allow|Allow This Time|Don’t Allow",
		`buttons ${idleDetection.buttons}`,
	);

	const allowThisTime = await answerSheet(app, "Allow This Time");
	const allowedOnce = await pageResult<string>(evaluate);

	const clipboardWrite = await page.evaluate<string>(
		"stateOf('clipboard-write')",
	);

	// The site's next page: what was kept.
	await app.send("closeTab", { tab });
	const next = context.fixtures.url("permissions.html?next");
	const nextPage = await app.page(await app.openTab(profile, next), next);

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

	expect(
		has("PaneWebContentsDelegate.requestPermission"),
		"trace lacks requestPermission",
	);

	expect(
		has("PanePermissionRequest.deny") &&
			has("PanePermissionRequest.allow") &&
			has("PanePermissionRequest.allowThisTime"),
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
		idleDetection,
		answers: [deny, allow, allowThisTime],
		denied,
		allowed,
		allowedOnce,
		kept,
		clipboardWrite,
		windows,
	};
};

export default permissionPrompt;
