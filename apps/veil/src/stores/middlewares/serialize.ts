export function serializeState(state: unknown): string {
	return JSON.stringify(state, (_key, value) =>
		typeof value === "function" ? undefined : value,
	);
}
