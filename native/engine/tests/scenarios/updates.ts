import { existsSync, mkdtempSync, readdirSync, statSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { invokeMenu } from "../lib/cua";
import { launchPane } from "../lib/pane";
import { expect, type Scenario } from "../run";

// The update check against a stand-in for GitHub's latest release (ADR 0002,
// DESIGN.md: Updates). Offered 99.0.0, Pane shows the sidebar pill; its click
// downloads the arm64 dmg into the downloads folder (a throwaway one here,
// left unopened), with progress; dismissing hides the pill. Offered nothing
// newer, Settings says Pane is up to date. Nothing reaches GitHub.

const VERSION = "99.0.0";

const updates: Scenario = async (context) => {
	const downloads = mkdtempSync(join(tmpdir(), "pane-updates-"));

	const app = await launchPane(context, {
		env: {
			PANE_UPDATE_URL: context.fixtures.url(`releases/latest?tag=v${VERSION}`),
			PANE_UPDATE_DOWNLOADS: downloads,
			PANE_UPDATE_OPEN: "0",
		},
	});

	const offered = await app.waitFor(
		"the update offered",
		(state) => state.update.status === "available" && state.update.pill,
		15_000,
	);

	expect(
		offered.update.available === VERSION,
		`offered ${offered.update.available}`,
	);

	await app.send("render", { path: join(context.out, "update-pill.png") });

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
	const dmg = join(downloads, `Pane-${VERSION}-arm64.dmg`);

	expect(
		existsSync(dmg) && statSync(dmg).size === 4 * 1024 * 1024,
		`downloaded ${JSON.stringify(files)}`,
	);

	expect(downloaded.update.pill, "the pill went away while it still has news");

	await app.send("dismissUpdate");
	const dismissed = await app.state();

	expect(!dismissed.update.pill, "dismissing did not hide the pill");

	await app.browser.close();

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
	// holds 0.8s at least).
	await invokeMenu(current.browser.pid, ["Pane", "Check for Updates\u2026"]);

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
		downloading: { progressSeen: partial },
		downloaded: {
			status: downloaded.update.status,
			file: `Pane-${VERSION}-arm64.dmg`,
			bytes: statSync(dmg).size,
		},
		dismissed: { pill: dismissed.update.pill },
		upToDate: upToDate.update,
		menuCheck: rechecking.update.status,
	};
};

export default updates;
