import path from "node:path";
import { app, net, session, shell } from "electron";
import type { StoreApi } from "zustand/vanilla";

import { type UpdateState, UpdateStatus } from "../stores/update-store";
import { isNewerVersion } from "./compare-versions";

const RELEASES_URL =
	"https://api.github.com/repos/andredezzy/pane/releases/latest";

const USER_AGENT = "Pane-Updater";
const DMG_ASSET_SUFFIX = "-arm64.dmg";

const CHECK_INTERVAL_MS = 24 * 60 * 60 * 1000;
const ETA_INCREASE_HOLD_MS = 5_000;
const MIN_CHECKING_MS = 800;
const RETRY_DELAYS_MS = [5 * 60_000, 15 * 60_000, 60 * 60_000];

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
// to IDLE. Returns whether the check completed so the scheduler can retry
// failures with backoff instead of waiting a full day.
export async function checkForUpdate(
	store: StoreApi<UpdateState>,
): Promise<boolean> {
	store.getState().startChecking();

	// The API usually answers faster than a state flip can be perceived; hold
	// the CHECKING state long enough that clicking the button visibly does
	// something even when the answer is "already up to date".
	const settle = new Promise<void>((resolve) =>
		setTimeout(resolve, MIN_CHECKING_MS),
	);

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

		await settle;

		if (!asset || !isNewerVersion(version, app.getVersion())) {
			store.getState().finishChecking({ available: null, checkedAt });

			return true;
		}

		store.getState().finishChecking({
			available: {
				version,
				dmgUrl: asset.browser_download_url,
				publishedAt: release.published_at,
			},
			checkedAt,
		});

		return true;
	} catch (error) {
		console.error("[updater] check for update failed:", error);

		await settle;
		store.getState().checkFailed();

		return false;
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

		// Whole-transfer average (curl's approach): received / elapsed has a
		// monotone denominator, so burst-and-stall delivery can't spike it, and
		// it grows more accurate as the download proceeds.
		const downloadStartedAt = Date.now();
		let publishedEtaSeconds: number | null = null;
		let etaIncreaseCandidateAt: number | null = null;

		item.on("updated", () => {
			const total = item.getTotalBytes();
			const received = item.getReceivedBytes();

			if (total <= 0) {
				return;
			}

			const now = Date.now();
			const elapsedSeconds = (now - downloadStartedAt) / 1000;
			const bytesPerSecond = elapsedSeconds > 0 ? received / elapsedSeconds : 0;

			const rawEtaSeconds =
				bytesPerSecond > 0
					? Math.max(0, Math.round((total - received) / bytesPerSecond))
					: null;

			// Hysteresis on top: decreases publish immediately (a countdown), but
			// an increase must persist for a while before it may move the number
			// up — a transient slowdown never drags the estimate backwards.
			if (rawEtaSeconds !== null) {
				if (
					publishedEtaSeconds === null ||
					rawEtaSeconds <= publishedEtaSeconds
				) {
					publishedEtaSeconds = rawEtaSeconds;
					etaIncreaseCandidateAt = null;
				} else if (etaIncreaseCandidateAt === null) {
					etaIncreaseCandidateAt = now;
				} else if (now - etaIncreaseCandidateAt > ETA_INCREASE_HOLD_MS) {
					publishedEtaSeconds = rawEtaSeconds;
					etaIncreaseCandidateAt = null;
				}
			}

			store.getState().setDownloadProgress({
				progress: received / total,
				etaSeconds: publishedEtaSeconds,
			});
		});

		item.once("done", (_doneEvent, state) => {
			if (state === "completed") {
				shell.openPath(savePath);
				store.getState().finishDownloading();
			} else {
				store.getState().downloadFailed();
			}
		});
	});

	session.defaultSession.downloadURL(available.dmgUrl);
}

// A failed check (offline at launch, rate-limited) retries with backoff
// instead of leaving the user unaware of an update for a whole day; a
// completed check returns to the daily cadence.
export function scheduleUpdateChecks(store: StoreApi<UpdateState>): void {
	let retryIndex = 0;

	const run = async () => {
		const completed = await checkForUpdate(store);

		if (completed) {
			retryIndex = 0;
			setTimeout(run, CHECK_INTERVAL_MS);
		} else {
			const delay =
				RETRY_DELAYS_MS[Math.min(retryIndex, RETRY_DELAYS_MS.length - 1)];

			retryIndex += 1;
			setTimeout(run, delay);
		}
	};

	void run();
}
