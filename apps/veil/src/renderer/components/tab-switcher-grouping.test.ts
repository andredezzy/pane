import { describe, expect, it } from "vitest";

import { ProfileColor } from "../../constants/profile-colors";
import {
	type GroupingProfile,
	groupMruTabs,
	initialSelectedIndex,
} from "./tab-switcher-grouping";

function makeTab(id: string, title = id, favicon = "") {
	return { id, title, favicon };
}

const profiles: GroupingProfile[] = [
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
	it("groups tabs by profile in first-seen (recency) order", () => {
		const groups = groupMruTabs(["p1", "w1", "p2"], profiles, 8);

		expect(groups.map((group) => group.id)).toEqual(["personal", "work"]);
		expect(groups[0].tabs.map((tab) => tab.id)).toEqual(["p1", "p2"]);
		expect(groups[1].tabs.map((tab) => tab.id)).toEqual(["w1"]);
	});

	it("caps the total number of tabs across groups", () => {
		const groups = groupMruTabs(["w1", "p1", "w2", "p2"], profiles, 2);

		const total = groups.reduce((sum, group) => sum + group.tabs.length, 0);

		expect(total).toBe(2);
		expect(groups.map((group) => group.id)).toEqual(["work", "personal"]);
	});

	it("skips tab ids that no profile owns", () => {
		const groups = groupMruTabs(["ghost", "w1"], profiles, 8);

		expect(groups.map((group) => group.id)).toEqual(["work"]);
		expect(groups[0].tabs.map((tab) => tab.id)).toEqual(["w1"]);
	});

	it("falls back to a loading title and empty favicon", () => {
		const sparse: GroupingProfile[] = [
			{
				id: "x",
				name: "X",
				color: ProfileColor.TEAL,
				tabs: [{ id: "x1", title: "", favicon: "" }],
			},
		];

		const groups = groupMruTabs(["x1"], sparse, 8);

		expect(groups[0].tabs[0].title).toBe("Loading...");
		expect(groups[0].tabs[0].favicon).toBe("");
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
});
