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
	// Recency decides WHICH tabs appear (the most-recently-used, capped) so the
	// switcher stays a recent-tabs list...
	const recentIds = new Set<string>();

	for (const tabId of mruHistory) {
		if (recentIds.size >= maxTabs) {
			break;
		}

		const owned = profiles.some((profile) =>
			profile.tabs.some((tab) => tab.id === tabId),
		);

		if (owned) {
			recentIds.add(tabId);
		}
	}

	// ...but they are presented in stable display order — profiles in sidebar
	// order, tabs in each profile's own order — so Ctrl+Tab walks the list
	// linearly top to bottom instead of hopping around by recency.
	const groups: ProfileGroup[] = [];

	for (const profile of profiles) {
		const tabs: SwitcherTab[] = [];

		for (const tab of profile.tabs) {
			if (recentIds.has(tab.id)) {
				tabs.push({
					id: tab.id,
					title: tab.title || "Loading...",
					favicon: tab.favicon,
				});
			}
		}

		if (tabs.length > 0) {
			groups.push({
				id: profile.id,
				name: profile.name,
				color: profile.color,
				tabs,
			});
		}
	}

	return groups;
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
