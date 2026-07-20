// Quantized to 5s steps so the label changes a few times a minute, not on
// every progress tick.
export function formatEtaSeconds(etaSeconds: number): string {
	const quantized = Math.max(5, Math.round(etaSeconds / 5) * 5);
	const minutes = Math.floor(quantized / 60);
	const seconds = quantized % 60;

	return minutes > 0 ? `${minutes}m ${seconds}s` : `${seconds}s`;
}
