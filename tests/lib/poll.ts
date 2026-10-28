// Waiting for a condition instead of a fixed time: `probe` runs until it
// returns something other than undefined, null, or false, which `poll`
// returns. Past `timeoutMs` it fails, naming what it waited for.
export async function poll<T>(
	description: string,
	probe: () =>
		| Promise<T | undefined | null | false>
		| T
		| undefined
		| null
		| false,
	timeoutMs = 10_000,
	intervalMs = 100,
): Promise<T> {
	const deadline = Date.now() + timeoutMs;

	for (;;) {
		const value = await probe();

		if (value !== undefined && value !== null && value !== false) {
			return value;
		}

		if (Date.now() > deadline) {
			throw new Error(`timed out waiting for ${description}`);
		}

		await Bun.sleep(intervalMs);
	}
}
