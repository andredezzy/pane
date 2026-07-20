// Compares plain major.minor.patch versions (an optional leading "v" and any
// prerelease/build suffix are ignored — GitHub release tags for this repo are
// always e.g. "v0.1.1", never "v0.1.1-beta.1").
function parseVersion(version: string): number[] {
	const numeric = version.replace(/^v/i, "").split(/[-+]/)[0];

	return numeric.split(".").map((part) => {
		const value = Number.parseInt(part, 10);

		if (Number.isNaN(value)) {
			throw new Error(`Invalid version segment "${part}" in "${version}"`);
		}

		return value;
	});
}

export function isNewerVersion(candidate: string, current: string): boolean {
	const candidateParts = parseVersion(candidate);
	const currentParts = parseVersion(current);
	const length = Math.max(candidateParts.length, currentParts.length);

	for (let index = 0; index < length; index++) {
		const candidatePart = candidateParts[index] ?? 0;
		const currentPart = currentParts[index] ?? 0;

		if (candidatePart !== currentPart) {
			return candidatePart > currentPart;
		}
	}

	return false;
}
