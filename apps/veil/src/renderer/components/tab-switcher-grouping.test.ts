import { describe, expect, it } from "vitest";

import { ProfileColor } from "../../constants/profile-colors";
import {
	computeGroupOffsets,
	cycleIndex,
	groupMruTabs,
	initialSelectedIndex,
	type ProfileSource,
} from "./tab-switcher-grouping";

function makeTab(id: string, title = id, favicon = "") {
	return { id, url: "", title, favicon, isLoaded: true };
}

const profiles: ProfileSource[] = [
	{
		id: "work",
		name: "Work",
		color: ProfileColor.BLUE,
		tabs: [makeTab("w1", "Gmail"), makeTab("w2", "Caixa")],
	},
	{
		id: "personal",
		name: "Personal",
		color: ProfileColor.ROSE,
		tabs: [makeTab("p1", "Proton"), makeTab("p2", "Inbox")],
	},
];

describe("groupMruTabs", () => {
	it("orders profiles and their tabs by most-recently-used", () => {
		// personal's p2 is most recent → personal first; within personal, p2 then p1
		// (most-recent-first, which differs from the natural [p1, p2] order).
		const groups = groupMruTabs(["p2", "w1", "p1"], profiles, 8);

		expect(groups.map((group) => group.id)).toEqual(["personal", "work"]);
		expect(groups[0].tabs.map((tab) => tab.id)).toEqual(["p2", "p1"]);
		expect(groups[1].tabs.map((tab) => tab.id)).toEqual(["w1"]);
	});

	it("caps the total number of tabs across groups", () => {
		const groups = groupMruTabs(["w1", "p1", "w2", "p2"], profiles, 2);

		const total = groups.reduce((sum, group) => sum + group.tabs.length, 0);

		expect(total).toBe(2);
		expect(groups.map((group) => group.id)).toEqual(["work", "personal"]);
		expect(groups[0].tabs.map((tab) => tab.id)).toEqual(["w1"]);
		expect(groups[1].tabs.map((tab) => tab.id)).toEqual(["p1"]);
	});

	it("skips tab ids that no profile owns", () => {
		const groups = groupMruTabs(["ghost", "w1"], profiles, 8);

		expect(groups.map((group) => group.id)).toEqual(["work"]);
		expect(groups[0].tabs.map((tab) => tab.id)).toEqual(["w1"]);
	});

	it("falls back to a loading title and leaves an absent favicon undefined", () => {
		const sparse: ProfileSource[] = [
			{
				id: "x",
				name: "X",
				color: ProfileColor.TEAL,
				tabs: [makeTab("x1", "")],
			},
		];

		const groups = groupMruTabs(["x1"], sparse, 8);

		expect(groups[0].tabs[0].title).toBe("Loading...");
		expect(groups[0].tabs[0].favicon).toBeUndefined();
	});

	it("orders tabs within a profile by most-recently-used", () => {
		// Used w2 then w1 → the group lists them most-recent-first [w2, w1].
		const groups = groupMruTabs(["w2", "w1"], profiles, 8);

		expect(groups.map((group) => group.id)).toEqual(["work"]);
		expect(groups[0].tabs.map((tab) => tab.id)).toEqual(["w2", "w1"]);
	});
});

describe("initialSelectedIndex", () => {
	const flattened = [makeTab("a"), makeTab("b"), makeTab("c")];

	it("selects the previous tab's flattened position", () => {
		expect(initialSelectedIndex(flattened, "c")).toBe(2);
	});

	it("falls back to the second item when the previous tab is gone", () => {
		expect(initialSelectedIndex(flattened, "missing")).toBe(1);
	});

	it("falls back to 0 when only one tab is present", () => {
		expect(initialSelectedIndex([makeTab("only")], undefined)).toBe(0);
	});

	it("returns 0 for an empty list", () => {
		expect(initialSelectedIndex([], undefined)).toBe(0);
	});
});

describe("computeGroupOffsets", () => {
	it("returns the running tab-count offset for each group", () => {
		const groups = groupMruTabs(["w1", "w2", "p1"], profiles, 8);

		expect(groups.map((group) => group.tabs.length)).toEqual([2, 1]);
		expect(computeGroupOffsets(groups)).toEqual([0, 2]);
	});

	it("returns an empty array for no groups", () => {
		expect(computeGroupOffsets([])).toEqual([]);
	});
});

describe("cycleIndex", () => {
	it("advances within range", () => {
		expect(cycleIndex(0, 1, 3)).toBe(1);
	});

	it("wraps forward past the last index", () => {
		expect(cycleIndex(2, 1, 3)).toBe(0);
	});

	it("wraps backward past the first index", () => {
		expect(cycleIndex(0, -1, 3)).toBe(2);
	});

	it("returns 0 for an empty list", () => {
		expect(cycleIndex(0, 1, 0)).toBe(0);
	});
});
