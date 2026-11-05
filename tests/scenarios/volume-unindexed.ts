import { join } from "node:path";
import { launchPane } from "../lib/pane";
import { expect, type Scenario } from "../run";

// Spotlight indexes nothing of the mounted volume (ADR 0121): it holds ciphertext
// now, but its names are the store's, and an index outlives a lock. mdutil says
// the mount's state ("unknown" on a volume out of the Finder, so the root says
// .metadata_never_index at every mount), and mdfind finds nothing there.
const volumeUnindexed: Scenario = async (context) => {
	const app = await launchPane(context);
	const profile = await app.createProfile("Spotlight");
	const url = context.fixtures.url("smoke.html?spotlight");
	const tab = await app.openTab(profile, url);
	await app.page(tab, url);

	const mount = join(app.dataDir, "Engine", "Volume");

	const status = await Bun.$`/usr/bin/mdutil -s ${mount}`
		.nothrow()
		.quiet()
		.text();

	// Long enough for an indexer to have started on what the profile wrote.
	await Bun.sleep(20_000);

	const found = (
		await Bun.$`/usr/bin/mdfind -onlyin ${mount} kMDItemFSName == "*"`
			.nothrow()
			.quiet()
			.text()
	)
		.split("\n")
		.filter(Boolean);

	const stores = await Bun.$`find ${mount} -maxdepth 1 -name ".*"`
		.nothrow()
		.quiet()
		.text();

	await app.quit();

	expect(
		found.length === 0,
		`Spotlight has ${found.length} items of the volume`,
	);

	return { status: status.trim(), found: found.length, hidden: stores.trim() };
};

export default volumeUnindexed;
