// The published Panes a scenario takes data from (PANE_EARLIER_BUILDS: their
// executables, comma-separated), each with its version, read from its bundle.
import { dirname, join } from "node:path";
import { plistString } from "./tools";

export type EarlierBuild = {
	executable: string;
	version: string;
	// It encrypts every profile's files itself (0.18.0 and later, ADR 0121):
	// its data has nothing to convert, and it opens this Pane's.
	encrypts: boolean;
};

// Whether `version` is `floor` or later, part by part.
function atLeast(version: string, floor: string): boolean {
	const parts = version.split(".").map(Number);

	for (const [at, least] of floor.split(".").map(Number).entries()) {
		const part = parts[at] ?? 0;

		if (part !== least) {
			return part > least;
		}
	}

	return true;
}

export function earlierBuilds(): EarlierBuild[] {
	return (process.env.PANE_EARLIER_BUILDS ?? "")
		.split(",")
		.filter(Boolean)
		.map((executable) => {
			const plist = join(dirname(executable), "..", "Info.plist");

			const version = plistString(plist, "CFBundleShortVersionString") ?? "";

			if (!/^\d+\.\d+\.\d+$/.test(version)) {
				throw new Error(`no Pane version in ${plist}`);
			}

			return { executable, version, encrypts: atLeast(version, "0.18.0") };
		});
}
