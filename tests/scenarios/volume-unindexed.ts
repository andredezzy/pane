import { launchPane } from "../lib/pane";
import { indexOfTheData } from "../lib/tools";
import { expect, type Scenario } from "../run";

// No indexer takes Pane's data (ADR 0121): it holds ciphertext now, but its
// names are the store's, and an index outlives a lock. On the Mac, Spotlight
// indexes nothing of the mounted volume: mdutil reads "unknown" on a volume
// out of the Finder, so the root says .metadata_never_index at every mount,
// and mdfind finds nothing there. On Windows, which has no volume (U5), the
// data folder carries the not-indexed attribute in its place, and NTFS gives
// it everything made in the folder after (lib/tools.ts).
const volumeUnindexed: Scenario = async (context) => {
	const app = await launchPane(context);
	const profile = await app.createProfile("Spotlight");
	await app.openPage(profile, context.fixtures.url("smoke.html?spotlight"));

	const { refused, indexable } = await indexOfTheData(app.dataDir);

	await app.quit();

	expect(refused, "the data doesn't refuse the index");

	expect(
		indexable.length === 0,
		`an indexer may take ${indexable.length} items of the data: ${indexable.slice(0, 5).join(", ")}`,
	);
};

export default volumeUnindexed;
