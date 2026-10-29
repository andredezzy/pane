import { spawnSync } from "node:child_process";
import { existsSync, readdirSync, statSync } from "node:fs";
import { join } from "node:path";
import { invokeMenu, isScreenLocked } from "../lib/cua";
import { type AppState, launchPane, type PaneApp } from "../lib/pane";
import { expect, type Scenario } from "../run";

// The update check against a stand-in for GitHub's latest release (ADR 0002,
// DESIGN.md: Updates). Offered 99.0.0, Pane shows the sidebar pill, which a
// check keeps up; its click downloads the arm64 dmg into the downloads folder
// (a throwaway one here, left unopened), with progress. A dmg that matches
// the release's SHA-256 leaves quarantine (ADR 0090); one that differs is
// deleted, and the update offered again. Once downloaded it stays downloaded,
// and dismissing hides the pill. Offered nothing newer, Settings says Pane is
// up to date. Nothing reaches GitHub.

const VERSION = "99.0.0";
const DMG = `Pane-${VERSION}-arm64.dmg`;

// Pane quarantines what it writes (LSFileQuarantineEnabled).
const quarantined = (file: string) =>
	spawnSync("xattr", ["-p", "com.apple.quarantine", file]).status === 0;

// The update's state every 25 ms for `ms`: a blink shows between samples.
async function sample(app: PaneApp, ms: number) {
	const seen: AppState["update"][] = [];
	const end = Date.now() + ms;

	while (Date.now() < end) {
		seen.push((await app.state()).update);
		await Bun.sleep(25);
	}

	return seen;
}

const describe = (seen: AppState["update"][]) =>
	JSON.stringify([
		...new Set(seen.map((update) => `${update.status} pill=${update.pill}`)),
	]);

const updates: Scenario = async (context) => {
	const offerFor = (query: string, downloads: string) =>
		launchPane(context, {
			env: {
				PANE_UPDATE_URL: context.fixtures.url(
					`releases/latest?tag=v${VERSION}${query}`,
				),
				PANE_UPDATE_DOWNLOADS: downloads,
				PANE_UPDATE_OPEN: "0",
			},
		});

	const offerOf = (app: PaneApp) =>
		app.waitFor(
			"the update offered",
			(state) => state.update.status === "available" && state.update.pill,
			15_000,
		);

	const downloads = context.tempDir("pane-updates-");
	const app = await offerFor("", downloads);
	const offered = await offerOf(app);

	expect(
		offered.update.available === VERSION,
		`offered ${offered.update.available}`,
	);

	await app.send("render", { path: join(context.out, "update-pill.png") });

	// A check while the update is on offer (the app menu's) leaves the pill
	// up: it blinked for the check's 0.8 s.
	await app.send("checkUpdates");
	const whileOffered = await sample(app, 1_200);

	expect(
		whileOffered.every(
			(update) => update.pill && update.status === "available",
		),
		`the offer changed during a check: ${describe(whileOffered)}`,
	);

	await app.send("downloadUpdate");

	// The progress the pill shows on the way.
	const seen: number[] = [];

	const downloaded = await app.waitFor(
		"the download done",
		(state) => {
			if (
				state.update.status === "downloading" &&
				state.update.progress !== null
			) {
				seen.push(state.update.progress);
			}

			return state.update.status === "downloaded";
		},
		30_000,
	);

	const partial = seen.filter((progress) => progress > 0 && progress < 1);

	expect(partial.length > 0, `no progress on the way: ${JSON.stringify(seen)}`);

	expect(
		partial.every(
			(progress, index) => index === 0 || progress >= (partial[index - 1] ?? 0),
		),
		`progress went backwards: ${JSON.stringify(partial)}`,
	);

	const files = readdirSync(downloads);
	const dmg = join(downloads, DMG);

	expect(
		existsSync(dmg) && statSync(dmg).size === 4 * 1024 * 1024,
		`downloaded ${JSON.stringify(files)}`,
	);

	// It matched the release's SHA-256: out of quarantine, so the app copied
	// from it opens without Gatekeeper's refusal.
	expect(!quarantined(dmg), "the verified dmg is still quarantined");
	expect(downloaded.update.pill, "the pill went away while it still has news");

	// A check once downloaded leaves it downloaded; the pill's click opens
	// the dmg again rather than downloading another.
	await app.send("checkUpdates");
	const whileDownloaded = await sample(app, 1_200);

	expect(
		whileDownloaded.every(
			(update) => update.pill && update.status === "downloaded",
		),
		`the download was forgotten during a check: ${describe(whileDownloaded)}`,
	);

	await app.send("downloadUpdate");
	await Bun.sleep(300);
	const clickedAgain = await app.state();

	expect(
		clickedAgain.update.status === "downloaded" &&
			readdirSync(downloads).length === 1,
		`the click downloaded again: ${clickedAgain.update.status}, ${JSON.stringify(readdirSync(downloads))}`,
	);

	await app.send("dismissUpdate");
	const dismissed = await app.state();

	expect(!dismissed.update.pill, "dismissing did not hide the pill");

	await app.browser.close();

	// A dmg that differs from the release's SHA-256 is deleted, and the
	// update is offered again.
	const mismatchDownloads = context.tempDir("pane-updates-");
	const mismatch = await offerFor("&digest=wrong", mismatchDownloads);
	await offerOf(mismatch);
	const started = await mismatch.send("downloadUpdate");

	const refused = await mismatch.waitFor(
		"the mismatch refused",
		(state) => state.update.status === "available",
		30_000,
	);

	const kept = readdirSync(mismatchDownloads);

	expect(
		started.state.update.status === "downloading" &&
			refused.update.pill &&
			kept.length === 0,
		`kept ${JSON.stringify(kept)}`,
	);

	await mismatch.browser.close();

	// A release without a digest still downloads, and its dmg stays
	// quarantined: Gatekeeper asks before the app inside first opens.
	const unverifiedDownloads = context.tempDir("pane-updates-");
	const unverified = await offerFor("&digest=none", unverifiedDownloads);
	await offerOf(unverified);
	await unverified.send("downloadUpdate");

	await unverified.waitFor(
		"the unverified download done",
		(state) => state.update.status === "downloaded",
		30_000,
	);

	const unverifiedQuarantined = quarantined(join(unverifiedDownloads, DMG));

	expect(unverifiedQuarantined, "a dmg without a digest left quarantine");

	await unverified.browser.close();

	// Nothing newer: up to date, no pill.
	const current = await launchPane(context);

	const upToDate = await current.waitFor(
		"the check done",
		(state) => state.update.checked && state.update.status === "idle",
		15_000,
	);

	expect(
		!upToDate.update.pill && upToDate.update.available === null,
		"an old release was offered",
	);

	// The app menu's Check for Updates… checks again (the checking state
	// holds 0.8s at least). Accessibility, which chooses it as a user does,
	// is off on a locked screen: the menu item's own action runs instead.
	if (await isScreenLocked()) {
		context.defer(
			"Check for Updates… chosen through accessibility: off on a locked screen",
		);

		await current.send("menu", { path: ["Pane", "Check for Updates…"] });
	} else {
		await invokeMenu(current.browser.pid, ["Pane", "Check for Updates…"]);
	}

	const rechecking = await current.waitFor(
		"checking from the menu",
		(state) => state.update.status === "checking",
		5_000,
	);

	await current.waitFor(
		"the menu's check done",
		(state) => state.update.status === "idle",
		15_000,
	);

	await current.send("showPage", { page: "settings" });
	await Bun.sleep(300);

	await current.send("render", {
		path: join(context.out, "update-settings.png"),
	});

	return {
		offered: offered.update,
		checkWhileOffered: describe(whileOffered),
		downloading: { progressSeen: partial },
		downloaded: {
			status: downloaded.update.status,
			file: DMG,
			bytes: statSync(dmg).size,
			quarantined: quarantined(dmg),
		},
		checkWhileDownloaded: describe(whileDownloaded),
		dismissed: { pill: dismissed.update.pill },
		mismatch: { status: refused.update.status, kept },
		unverified: { quarantined: unverifiedQuarantined },
		upToDate: upToDate.update,
		menuCheck: rechecking.update.status,
	};
};

export default updates;
