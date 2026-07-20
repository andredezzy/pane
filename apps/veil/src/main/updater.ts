import path from "node:path";
import { app, net, session, shell } from "electron";
import type { StoreApi } from "zustand/vanilla";

import { type UpdateState, UpdateStatus } from "../stores/update-store";
import { isNewerVersion } from "./compare-versions";

const RELEASES_URL =
	"https://api.github.com/repos/andredezzy/pane/releases/latest";

const USER_AGENT = "Pane-Updater";
const DMG_ASSET_SUFFIX = "-arm64.dmg";

const INITIAL_CHECK_DELAY_MS = 10_000;
const CHECK_INTERVAL_MS = 24 * 60 * 60 * 1000;

interface GitHubReleaseAsset {
	name: string;
	browser_download_url: string;
}

interface GitHubRelease {
	tag_name: string;
	published_at: string;
	assets: GitHubReleaseAsset[];
}

// Never throws: a stale rate limit, a flaky connection, or the user being
// offline should never crash the app or nag them — the store just falls back
// to IDLE and the next scheduled check tries again.
export async function checkForUpdate(
	store: StoreApi<UpdateState>,
): Promise<void> {
	store.getState().startChecking();

	try {
		const response = await net.fetch(RELEASES_URL, {
			headers: { "User-Agent": USER_AGENT },
		});

		if (!response.ok) {
			throw new Error(`GitHub releases request failed: ${response.status}`);
		}

		const release = (await response.json()) as GitHubRelease;
		const version = release.tag_name.replace(/^v/i, "");
		const checkedAt = new Date().toISOString();

		const asset = release.assets.find((candidate) =>
			candidate.name.endsWith(DMG_ASSET_SUFFIX),
		);

		if (!asset || !isNewerVersion(version, app.getVersion())) {
			store.getState().finishChecking({ available: null, checkedAt });

			return;
		}

		store.getState().finishChecking({
			available: {
				version,
				dmgUrl: asset.browser_download_url,
				publishedAt: release.published_at,
			},
			checkedAt,
		});
	} catch (error) {
		console.error("[updater] check for update failed:", error);
		store.getState().checkFailed();
	}
}

// One click downloads the dmg to the user's Downloads folder and opens it so
// Finder mounts it; the user drags Pane to /Applications themselves. No modal
// dialogs, no force-quitting the app — on any failure this just falls back to
// AVAILABLE so the user can retry from the same banner.
export function downloadUpdate(store: StoreApi<UpdateState>): void {
	const { status, available } = store.getState();

	if (status !== UpdateStatus.AVAILABLE || !available) {
		return;
	}

	store.getState().startDownloading();

	session.defaultSession.once("will-download", (_event, item) => {
		const savePath = path.join(app.getPath("downloads"), item.getFilename());
		item.setSavePath(savePath);

		item.once("done", (_doneEvent, state) => {
			if (state === "completed") {
				shell.openPath(savePath);
			}

			store.getState().finishDownloading();
		});
	});

	session.defaultSession.downloadURL(available.dmgUrl);
}

export function scheduleUpdateChecks(store: StoreApi<UpdateState>): void {
	setTimeout(() => {
		void checkForUpdate(store);
	}, INITIAL_CHECK_DELAY_MS);

	setInterval(() => {
		void checkForUpdate(store);
	}, CHECK_INTERVAL_MS);
}
