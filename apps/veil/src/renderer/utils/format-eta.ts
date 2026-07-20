// A handful of coarse buckets instead of a live countdown: an erratic CDN
// swings any precise estimate constantly, but "~3min" can only ever step to
// "~2min" — jumps are structurally impossible, not merely dampened.
export function formatEtaSeconds(etaSeconds: number): string {
	if (etaSeconds < 45) {
		return "<1min";
	}

	return `~${Math.max(1, Math.round(etaSeconds / 60))}min`;
}
