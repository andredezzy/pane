import type { ProfileColor } from "../../constants/profile-colors";

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

export function groupMruTabs(
	mruHistory: string[],
	profiles: ProfileGroup[],
	maxTabs: number,
): ProfileGroup[] {
	const groups: ProfileGroup[] = [];
	const groupsByProfileId = new Map<string, ProfileGroup>();

	let count = 0;

	for (const tabId of mruHistory) {
		if (count >= maxTabs) {
			break;
		}

		for (const profile of profiles) {
			const tab = profile.tabs.find((tab) => tab.id === tabId);

			if (!tab) {
				continue;
			}

			let group = groupsByProfileId.get(profile.id);

			if (!group) {
				group = {
					id: profile.id,
					name: profile.name,
					color: profile.color,
					tabs: [],
				};

				groupsByProfileId.set(profile.id, group);
				groups.push(group);
			}

			group.tabs.push({
				id: tab.id,
				title: tab.title || "Loading...",
				favicon: tab.favicon || "",
			});

			count++;

			break;
		}
	}

	return groups;
}

export function initialSelectedIndex(
	flattened: SwitcherTab[],
	previousTabId: string | undefined,
): number {
	const index = flattened.findIndex((tab) => tab.id === previousTabId);

	return index >= 0 ? index : Math.min(1, flattened.length - 1);
}
