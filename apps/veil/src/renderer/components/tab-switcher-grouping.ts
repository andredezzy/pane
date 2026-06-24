import type { ProfileColor } from "../../constants/profile-colors";
import type { BrowserProfile } from "../../stores/profile-store";

export interface SwitcherTab {
	id: string;
	title: string;
	favicon: string;
}

export interface ProfileGroup {
	id: string;
	name: string;
	color: ProfileColor;
	tabs: SwitcherTab[];
}

export type ProfileSource = Pick<
	BrowserProfile,
	"id" | "name" | "color" | "tabs"
>;

export function groupMruTabs(
	mruHistory: string[],
	profiles: ProfileSource[],
	maxTabs: number,
): ProfileGroup[] {
	const profileByTabId = new Map<string, ProfileSource>();

	for (const profile of profiles) {
		for (const tab of profile.tabs) {
			profileByTabId.set(tab.id, profile);
		}
	}

	// Recency decides WHICH tabs appear (the most-recently-used, capped).
	const recentIds = new Set<string>();

	for (const tabId of mruHistory) {
		if (recentIds.size >= maxTabs) {
			break;
		}

		if (profileByTabId.has(tabId)) {
			recentIds.add(tabId);
		}
	}

	// Profiles are ordered by their most-recently-used tab — the first profile
	// seen while walking the recency history comes first.
	const orderedProfiles: ProfileSource[] = [];
	const seen = new Set<string>();

	for (const tabId of mruHistory) {
		if (!recentIds.has(tabId)) {
			continue;
		}

		const profile = profileByTabId.get(tabId);

		if (profile && !seen.has(profile.id)) {
			seen.add(profile.id);
			orderedProfiles.push(profile);
		}
	}

	// Within a profile, tabs keep their natural order so Ctrl+Tab walks each
	// profile's tabs linearly top to bottom.
	return orderedProfiles.map((profile) => {
		const tabs = profile.tabs
			.filter((tab) => recentIds.has(tab.id))
			.map((tab) => ({
				id: tab.id,
				title: tab.title || "Loading...",
				favicon: tab.favicon,
			}));

		return {
			id: profile.id,
			name: profile.name,
			color: profile.color,
			tabs,
		};
	});
}

export function initialSelectedIndex(
	flattened: SwitcherTab[],
	previousTabId: string | undefined,
): number {
	if (flattened.length === 0) {
		return 0;
	}

	const index = flattened.findIndex((tab) => tab.id === previousTabId);

	return index >= 0 ? index : Math.min(1, flattened.length - 1);
}

export function computeGroupOffsets(groups: ProfileGroup[]): number[] {
	const offsets: number[] = [];

	let running = 0;

	for (const group of groups) {
		offsets.push(running);
		running += group.tabs.length;
	}

	return offsets;
}

export function cycleIndex(
	current: number,
	delta: number,
	length: number,
): number {
	if (length === 0) {
		return 0;
	}

	return (current + delta + length) % length;
}
