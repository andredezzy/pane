import type { ProfileColor } from "../../constants/profile-colors";
import type { BrowserProfile } from "../../stores/profile-store";

export interface SwitcherTab {
	id: string;
	title: string;
	favicon?: string;
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

	// Walk the recency history once: both the profile (group) order and the tab
	// order within each group follow most-recently-used first. Capped to keep the
	// switcher a recent-tabs list.
	const groups: ProfileGroup[] = [];
	const groupByProfileId = new Map<string, ProfileGroup>();

	let count = 0;

	for (const tabId of mruHistory) {
		if (count >= maxTabs) {
			break;
		}

		const profile = profileByTabId.get(tabId);

		if (!profile) {
			continue;
		}

		const tab = profile.tabs.find((candidate) => candidate.id === tabId);

		if (!tab) {
			continue;
		}

		let group = groupByProfileId.get(profile.id);

		if (!group) {
			group = {
				id: profile.id,
				name: profile.name,
				color: profile.color,
				tabs: [],
			};

			groupByProfileId.set(profile.id, group);
			groups.push(group);
		}

		group.tabs.push({
			id: tab.id,
			title: tab.title || "Loading...",
			favicon: tab.favicon || undefined,
		});

		count++;
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
