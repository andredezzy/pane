// The MRU history is most-recent-first and spans every profile. Pick the most
// recently used tab that is still present in `tabs`, falling back to the last
// one when none of them appear in the history.
export function mostRecentTab<T extends { id: string }>(
	tabs: T[],
	mruHistory: string[],
): T | undefined {
	for (const tabId of mruHistory) {
		const tab = tabs.find((candidate) => candidate.id === tabId);

		if (tab) {
			return tab;
		}
	}

	return tabs[tabs.length - 1];
}
