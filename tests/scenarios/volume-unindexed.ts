import { existsSync } from "node:fs";
import { join } from "node:path";
import { launchPane } from "../lib/pane";
import type { NotOnWindows } from "../lib/platform";
import { expect, type Scenario } from "../run";

// Where it runs (lib/platform.ts).
export const notOnWindows: NotOnWindows = {
	needsDesign:
		"Spotlight's marker on the Mac's volume; Windows' data folder takes the not-indexed attribute in its place (W3)",
};

// Spotlight indexes nothing of the mounted volume (ADR 0121): it holds ciphertext
// now, but its names are the store's, and an index outlives a lock. mdutil reads
// "unknown" on a volume out of the Finder, so the root says .metadata_never_index
// at every mount, and mdfind finds nothing there.
const volumeUnindexed: Scenario = async (context) => {
	const app = await launchPane(context);
	const profile = await app.createProfile("Spotlight");
	await app.openPage(profile, context.fixtures.url("smoke.html?spotlight"));

	const mount = join(app.dataDir, "Engine", "Volume");
	const refused = existsSync(join(mount, ".metadata_never_index"));

	const found = (
		await Bun.$`/usr/bin/mdfind -onlyin ${mount} kMDItemFSName == "*"`
			.nothrow()
			.quiet()
			.text()
	)
		.split("\n")
		.filter(Boolean);

	await app.quit();

	expect(refused, "the volume's root lacks .metadata_never_index");

	expect(
		found.length === 0,
		`Spotlight has ${found.length} items of the volume`,
	);
};

export default volumeUnindexed;
