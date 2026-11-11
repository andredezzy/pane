import { signpostFrameIntervals } from "../lib/signposts";
import { serveSlowRelease } from "../lib/slow-release";
import { launchCopy, standInDmg } from "../lib/update-copies";
import type { Scenario } from "../run";

// The update's frames, from the app's signposts (pane/ui/interaction_log.h;
// docs/plans/views-migration.md, K1): an offer to the pill's first frame
// ("Show update"), and each share downloaded to the frame showing it
// ("Download progress"), over `ROUNDS` launches, each a copy in a folder of
// its own offered a 6 MB stand-in dmg served at 640 KB/s.

const ROUNDS = 5;

type Stats = { runs: number; median: number; p90: number; max: number };

function stats(values: number[]): Stats | null {
	if (values.length === 0) {
		return null;
	}

	const sorted = [...values].sort((a, b) => a - b);

	const at = (share: number) =>
		sorted[Math.min(sorted.length - 1, Math.floor(share * sorted.length))] ?? 0;

	return {
		runs: sorted.length,
		median: at(0.5),
		p90: at(0.9),
		max: sorted.at(-1) ?? 0,
	};
}

const updateTiming: Scenario = async (context) => {
	const work = context.tempDir("pane-update-timing-");
	const dmg = await standInDmg(work, "99.0.0", "99.0.0", 6 * 1024 * 1024);
	const server = await serveSlowRelease(dmg);

	const shown: number[] = [];
	const progress: number[] = [];

	try {
		for (let round = 0; round < ROUNDS; round++) {
			const intervals = await signpostFrameIntervals(async () => {
				const { app } = await launchCopy(context, server.releases, undefined, {
					PANE_UPDATE_INSTALL: "0",
				});

				await app.waitFor(
					"the update offered",
					(state) => state.update.pill,
					15_000,
				);

				await app.send("downloadUpdate");

				await app.waitFor(
					"the download done",
					(state) => state.update.status === "downloaded",
					60_000,
				);

				const pid = app.browser.pid;
				await app.browser.close();

				return pid;
			});

			shown.push(...(intervals["Show update"] ?? []).map(({ ms }) => ms));

			progress.push(
				...(intervals["Download progress"] ?? []).map(({ ms }) => ms),
			);
		}
	} finally {
		server.stop();
	}

	return {
		rounds: ROUNDS,
		showUpdate: stats(shown),
		downloadProgress: stats(progress),
	};
};

export default updateTiming;
