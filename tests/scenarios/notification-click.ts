import { existsSync, rmSync } from "node:fs";
import {
	CENTER_LISTS_A_COPYS_NOTIFICATIONS,
	clickNotification,
	persistentNotificationId,
	type SiteNotification,
	TOAST_CLICKS_ARE_LAUNCHES,
	TOASTS_NEED_A_START_SHORTCUT,
	toastLaunchId,
} from "../lib/browser";
import {
	type AppState,
	dataClosed,
	dataOpen,
	launchPane,
	type PaneApp,
	setPinBeforeLaunch,
} from "../lib/pane";
import { poll } from "../lib/poll";
import { waitForExit } from "../lib/processes";
import { answerSheet, waitForSheet } from "../lib/surfaces";
import {
	type CenterToast,
	centerToasts,
	shortcutAppId,
	startShortcut,
} from "../lib/tools";
import { expect, type Scenario } from "../run";

// A site's notification, clicked in the system's own center, reaches the site
// (the user's call, 2026-11-16; ADR 0109): its service worker hears
// notificationclick, and its page comes up in Pane's window, in the
// notification's profile, a new tab selected or one of its pages focused,
// from another profile shown. With Pane locked the click shows the PIN,
// nothing of the profile opens in the engine, and the site hears the click
// once the PIN is in. On Windows a toast's click is a launch of chrome.exe
// with its launch id (notification_helper.exe), which the running copy's
// process singleton takes, and which starts a closed Pane, locked at its
// launch: Chrome's own startup would have opened the toast's profile before
// the PIN, and Pane's callback read pages alone, so the click showed the
// window and nothing more (W8b). On the Mac, macOS's center hands the click
// to the browser itself, as the channel hands it here, with no click on the
// screen, and Chrome's handler hands it to Pane (patch 0004) as Windows'
// launch does: behind the lock it waits for the PIN, where Chrome's handler
// loaded the profile outside Pane's lock, and the profile never came back
// after the PIN (W8d). A profile's notifications leave the system's center
// as the profile closes, at every Lock, and every one of Pane's at Exit, on
// both systems (the user's call, 2026-11-18): Chrome's bridge takes them from
// macOS's center, and Pane from Windows', whose bridge took none, so a site's
// words stayed there with Pane locked or closed (W10g). The runner reads
// Windows' center: the profile's toasts there before the Lock, a
// requireInteraction one among them, none after it, none of Pane's after
// Exit. So a click behind the lock is one that lands as the lock comes, on
// both systems, and with Pane closed only a notification a crash left can be
// clicked, which on Windows starts Pane locked; nothing of Pane's is in
// Windows' center once that launch has started (W10h). A toast is Windows' only
// while Start holds Pane's shortcut, which a test copy's first launch makes
// in its own home: with it deleted, the notification shows in Chrome's own
// center, and the Lock still closes the profile and leaves Pane running, the
// popup gone (W10f).
const notificationClick: Scenario = async (context) => {
	const dataDir = context.tempDir("pane-notification-");
	await setPinBeforeLaunch(context, dataDir, "1234");
	let app = await launchPane(context, { dataDir });
	await app.send("setLockAfter", { minutes: null });
	await unlock(app);

	// Unlocked with no page to show, the keys leave the PIN's field as the
	// PIN screen goes (W8d: they stayed in it, hidden, where a screen reader
	// still read it).
	const opened = await app.state();

	expect(
		!opened.focus.startsWith("pane.lockScreen"),
		`the keys after the PIN, with no page to show: ${opened.focus}`,
	);

	const profileId = await app.createProfile("Alerts");
	const pageUrl = context.fixtures.url("notification.html?page");
	const { tab: pageTab, page } = await app.openPage(profileId, pageUrl);
	await page.evaluate("window.ready");

	await page.gesture("ask()");
	await waitForSheet(app, "permission");
	await answerSheet(app, "Allow");

	await poll(
		"notifications allowed",
		async () =>
			(await page.evaluate<string>("Notification.permission")) === "granted",
		10_000,
	);

	const notification = (tag: string): SiteNotification => ({
		profileId,
		origin: context.fixtures.origin,
		tag,
	});

	// Each shown while the profile is open, as a site shows them while its
	// pages run, one that stays until its user acts on it where the runner
	// reads the center (Windows).
	const shownTags = [
		"running",
		"focus",
		"locked",
		...(CENTER_LISTS_A_COPYS_NOTIFICATIONS ? ["staying"] : []),
	];

	for (const [tag, focus, stays] of [
		["running", null, false],
		["focus", "notification.html?page", false],
		["locked", null, false],
		...(CENTER_LISTS_A_COPYS_NOTIFICATIONS ? [["staying", null, true]] : []),
	] as [string, string | null, boolean][]) {
		// Shown once Chrome has it, as showNotification resolves: what the
		// system's center lists of it (getNotifications) is the center's own,
		// which lists a test copy's banner on the Mac only seconds after its
		// show (W10h; W8d read it at once and found none).
		await page.evaluate(
			`show(${JSON.stringify(tag)}, ${JSON.stringify(focus ?? undefined)}, ${stays})`,
		);
	}

	// The AppUserModelID Windows' center keeps the copy's toasts under, its
	// own Start shortcut's, read before the last part deletes that shortcut.
	const appId = CENTER_LISTS_A_COPYS_NOTIFICATIONS
		? await shortcutAppId(startShortcut(app.home))
		: "";

	if (CENTER_LISTS_A_COPYS_NOTIFICATIONS) {
		expect(appId !== "", "the copy's Start shortcut names no AppUserModelID");
		await inCenter(appId, shownTags.map(notification));
	}

	const click = async (tag: string) => {
		const failed = await clickNotification(
			(name: string, args: Record<string, unknown>) => app.send(name, args),
			{ binary: context.binary, pid: app.browser.pid, dataDir },
			notification(tag),
		);

		expect(failed === null, `${failed}`);
	};

	// Running and open: the site's page, opened by its worker, a new tab of
	// the notification's profile, selected.
	await click("running");

	const running = await app.waitFor(
		"the page the click opened, selected in its profile",
		(state) => clicked(state, profileId, "running")?.id === state.activeTabID,
		30_000,
	);

	expect(
		running.window.visible && running.activeProfileID === profileId,
		`after the click: ${JSON.stringify({ visible: running.window.visible, profile: running.activeProfileID })}`,
	);

	// Another profile shown: the site's own page focused by its click, its
	// profile's tab selected.
	const other = await app.createProfile("Other", "ROSE");
	await app.openTab(other, context.fixtures.url("light.html?other"));

	await app.waitFor(
		"the other profile shown",
		(state) => state.activeProfileID === other,
	);

	await click("focus");

	await app.waitFor(
		"the site's page focused by the click",
		(state) => state.activeTabID === pageTab,
		30_000,
	);

	// Locked: the PIN shows, the profile stays closed, and the site hears the
	// click once the PIN is in, on both systems (on the Mac a click that
	// lands as the lock comes).
	await app.send("lockNow");

	await app.waitFor(
		"locked, the profiles gone",
		(state) => state.security.locked && state.profiles.length === 0,
		30_000,
	);

	// The profile's notifications out of Windows' center with it, the one
	// that stays included: nothing of the site's left there behind the PIN.
	if (CENTER_LISTS_A_COPYS_NOTIFICATIONS) {
		await outOfCenter(appId, "the Lock", profileId);
	}

	await poll("the data closed", () => dataClosed(app), 30_000);

	await click("locked");
	await Bun.sleep(3_000);
	const behind = await app.state();

	const whileLocked = {
		locked: behind.security.locked,
		visible: behind.window.visible,
		engineProfiles: behind.engineProfiles,
		closed: await dataClosed(app),
	};

	expect(
		whileLocked.locked &&
			whileLocked.visible &&
			!whileLocked.engineProfiles.includes(profileId) &&
			whileLocked.closed,
		`a click behind the lock: ${JSON.stringify(whileLocked)}`,
	);

	await unlock(app);

	await app.waitFor(
		"the page the click behind the lock opened, once the PIN was in",
		(state) => clicked(state, profileId, "locked")?.id === state.activeTabID,
		60_000,
	);

	if (!TOAST_CLICKS_ARE_LAUNCHES) {
		return;
	}

	// A crash: the profile never closes, so its notifications stay in
	// Windows' center, a plain one and one that stays until its user acts on
	// it, and a click on one starts Pane. Nothing of Pane's is left there
	// once that launch has started, before any profile and before the PIN
	// (the user's call, 2026-11-18): the click, which Windows takes out of
	// the center as it starts Pane, is held for the PIN, the profile closed
	// until then, and the site then hears it.
	await showAgain(app, profileId, pageUrl, "crashed");
	await showAgain(app, profileId, pageUrl, "crashed-staying", true);
	const leftovers = [notification("crashed"), notification("crashed-staying")];
	await inCenter(appId, leftovers);
	const crashed = app.browser.pid;
	process.kill(crashed, "SIGKILL");
	expect(await waitForExit(crashed, 30_000), "Pane didn't end at its kill");
	// Its other processes end after it, before the next launch on its data.
	await Bun.sleep(1_500);
	await inCenter(appId, leftovers);

	app = await launchPane(context, {
		dataDir,
		args: [
			`--notification-launch-id=${toastLaunchId(notification("crashed"))}`,
		],
	});

	const launched = await app.state();
	const leftAtLaunch = await centerToasts(appId);

	expect(
		leftAtLaunch.length === 0,
		`Windows' center still lists after the launch that followed the crash: ${JSON.stringify(leftAtLaunch)}`,
	);

	expect(
		launched.security.locked && !launched.engineProfiles.includes(profileId),
		`the toast's launch: ${JSON.stringify({ locked: launched.security.locked, engineProfiles: launched.engineProfiles })}`,
	);

	await unlock(app);

	await app.waitFor(
		"the page the toast's launch opened, once the PIN was in",
		(state) => clicked(state, profileId, "crashed")?.id === state.activeTabID,
		60_000,
	);

	// Exit: every toast of Pane's leaves Windows' center.
	await showAgain(app, profileId, pageUrl, "exit");
	await inCenter(appId, [notification("exit")]);
	await app.quit();
	expect(await waitForExit(app.browser.pid, 30_000), "Pane didn't end at Quit");
	await outOfCenter(appId, "Exit");

	if (TOASTS_NEED_A_START_SHORTCUT) {
		app = await launchPane(context, { dataDir });
		await unlock(app);
		await lockWithChromesCenter(app, profileId, pageUrl);
	}
};

// The site's page opened in a new tab of the profile, at an address of its
// own, which shows the notification tagged `tag`, one that stays with
// `stays`.
async function showAgain(
	app: PaneApp,
	profileId: string,
	pageUrl: string,
	tag: string,
	stays = false,
) {
	const { page } = await app.openPage(
		profileId,
		pageUrl.replace("?page", `?shows=${tag}`),
	);

	await page.evaluate("window.ready");
	await page.evaluate(`show(${JSON.stringify(tag)}, undefined, ${stays})`);
}

// What Windows' center holds under `appId` lists each of `notifications`, as
// it shows.
async function inCenter(appId: string, notifications: SiteNotification[]) {
	const ids = notifications.map(persistentNotificationId);
	let listed: CenterToast[] = [];

	await poll(
		"the notifications in Windows' center",
		async () => {
			listed = await centerToasts(appId);

			return ids.every((id) => listed.some((toast) => toast.id === id));
		},
		15_000,
	).catch(() => {
		throw new Error(
			`Windows' center lists ${JSON.stringify(listed)}, not each of ${JSON.stringify(ids)}`,
		);
	});
}

// Windows' center holds nothing under `appId` of `profileId`'s, or with none
// named, of Pane's at all, soon after `moment`.
async function outOfCenter(appId: string, moment: string, profileId?: string) {
	let left: CenterToast[] = [];

	await poll(
		`nothing of the site's in Windows' center after ${moment}`,
		async () => {
			left = (await centerToasts(appId)).filter(
				(toast) => profileId === undefined || toast.profileId === profileId,
			);

			return left.length === 0;
		},
		10_000,
	).catch(() => {
		throw new Error(
			`Windows' center still lists after ${moment}: ${JSON.stringify(left)}`,
		);
	});
}

// Start's shortcut deleted, as a person may delete theirs: Chrome shows the
// site's notification in its own center, a popup window of Pane's process at
// the screen's corner, and each one there held its profile open, so the Lock
// that came while one showed found the profile open and quit Pane, the popup
// beside the PIN until then (W10f). The Lock takes it from Chrome's center:
// Pane stays, locked, the profile closed and the data with it, and nothing of
// the notification shows. The copy's own shortcut, which its first launch
// made as the install makes the user's (pane/app/data_launch_win.cc), is the
// one deleted: the user's Start menu is never read or written.
async function lockWithChromesCenter(
	app: PaneApp,
	profileId: string,
	pageUrl: string,
) {
	const link = startShortcut(app.home);
	expect(existsSync(link), `the copy's first launch made no Start shortcut`);
	rmSync(link);

	// The profile opens again behind the PIN, and asks Windows' center anew
	// whether it takes the toasts: without the shortcut, no.
	await app.send("lockNow");

	await app.waitFor(
		"locked, the profiles gone",
		(state) => state.security.locked && state.profiles.length === 0,
		30_000,
	);

	await unlock(app);
	const { page } = await app.openPage(profileId, pageUrl);
	await page.evaluate("window.ready");
	const before = (await app.state()).nativeWindows.map((each) => each.number);
	await page.evaluate(`show("center")`);

	const shown = await app.waitFor(
		"the notification's popup in Chrome's own center",
		(state) =>
			state.nativeWindows.some(
				(each) => each.role !== "main" && !before.includes(each.number),
			),
		15_000,
	);

	const popups = shown.nativeWindows.filter(
		(each) => each.role !== "main" && !before.includes(each.number),
	);

	await app.send("lockNow");

	await app.waitFor(
		"locked with the popup showing, the profiles gone",
		(state) => state.security.locked && state.profiles.length === 0,
		30_000,
	);

	await poll("the data closed", () => dataClosed(app), 30_000);

	// Past the Lock's 10 s for a profile to close, after which one still open
	// makes Pane quit (ADR 0116).
	await Bun.sleep(12_000);
	const behind = await app.state();

	const left = behind.nativeWindows.filter((each) =>
		popups.some((popup) => popup.number === each.number),
	);

	expect(
		behind.security.locked &&
			!behind.engineProfiles.includes(profileId) &&
			(await dataClosed(app)) &&
			left.length === 0,
		`the Lock with Chrome's own popup showing: ${JSON.stringify({ locked: behind.security.locked, engineProfiles: behind.engineProfiles, popups, left })}`,
	);

	await unlock(app);
}

async function unlock(app: PaneApp) {
	await app.send("pinKeys", { keys: "1234" });

	await app.waitFor(
		"unlocked, the data open",
		(state) => !state.security.locked && dataOpen(state),
		60_000,
	);
}

// The tab of `profileId` the click on the notification tagged `tag` opened.
function clicked(state: AppState, profileId: string, tag: string) {
	return state.profiles
		.find((profile) => profile.id === profileId)
		?.tabs.find((tab) => tab.url.endsWith(`notification.html?clicked=${tag}`));
}

export default notificationClick;
