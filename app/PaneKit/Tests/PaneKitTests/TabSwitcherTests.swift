// Ported from apps/veil/src/renderer/components/tab-switcher-grouping.test.ts.
import PaneKit
import Testing

private func makeTab(_ id: String, _ title: String? = nil) -> Tab {
	Tab(id: id, url: "", title: title ?? id, favicon: "")
}

private func makeProfile(_ id: String, _ name: String, _ color: ProfileColor, _ tabs: [Tab]) -> BrowserProfile {
	BrowserProfile(id: id, name: name, color: color, tabs: tabs, createdAt: "", updatedAt: "")
}

private let profiles = [
	makeProfile("work", "Work", .blue, [makeTab("w1", "Gmail"), makeTab("w2", "Caixa")]),
	makeProfile("personal", "Personal", .rose, [makeTab("p1", "Proton"), makeTab("p2", "Inbox")]),
]

@Suite("TabSwitcher.groups")
struct TabSwitcherGroupsTests {
	@Test("orders profiles and their tabs by most-recently-used")
	func ordersByRecency() {
		// personal's p2 is most recent → personal first; within personal, p2 then
		// p1 (most-recent-first, which differs from the natural [p1, p2] order).
		let groups = TabSwitcher.groups(mru: ["p2", "w1", "p1"], profiles: profiles, maxTabs: 8)

		#expect(groups.map(\.id) == ["personal", "work"])
		#expect(groups[0].tabs.map(\.id) == ["p2", "p1"])
		#expect(groups[1].tabs.map(\.id) == ["w1"])
	}

	@Test("caps the total number of tabs across groups")
	func capsTotal() {
		let groups = TabSwitcher.groups(mru: ["w1", "p1", "w2", "p2"], profiles: profiles, maxTabs: 2)

		#expect(groups.reduce(0) { $0 + $1.tabs.count } == 2)
		#expect(groups.map(\.id) == ["work", "personal"])
		#expect(groups[0].tabs.map(\.id) == ["w1"])
		#expect(groups[1].tabs.map(\.id) == ["p1"])
	}

	@Test("skips tab ids that no profile owns")
	func skipsUnowned() {
		let groups = TabSwitcher.groups(mru: ["ghost", "w1"], profiles: profiles, maxTabs: 8)

		#expect(groups.map(\.id) == ["work"])
		#expect(groups[0].tabs.map(\.id) == ["w1"])
	}

	@Test("falls back to a loading title and leaves an absent favicon nil")
	func loadingTitle() {
		let sparse = [makeProfile("x", "X", .teal, [makeTab("x1", "")])]
		let groups = TabSwitcher.groups(mru: ["x1"], profiles: sparse, maxTabs: 8)

		#expect(groups[0].tabs[0].title == "Loading...")
		#expect(groups[0].tabs[0].favicon == nil)
	}

	@Test("orders tabs within a profile by most-recently-used")
	func ordersWithinProfile() {
		// Used w2 then w1 → the group lists them most-recent-first [w2, w1].
		let groups = TabSwitcher.groups(mru: ["w2", "w1"], profiles: profiles, maxTabs: 8)

		#expect(groups.map(\.id) == ["work"])
		#expect(groups[0].tabs.map(\.id) == ["w2", "w1"])
	}
}

@Suite("TabSwitcher.initialSelectedIndex")
struct InitialSelectedIndexTests {
	private let flattened = ["a", "b", "c"].map { SwitcherTab(id: $0, title: $0) }

	@Test("selects the previous tab's flattened position")
	func previousTab() {
		#expect(TabSwitcher.initialSelectedIndex(flattened, previousTabID: "c") == 2)
	}

	@Test("falls back to the second item when the previous tab is gone")
	func previousGone() {
		#expect(TabSwitcher.initialSelectedIndex(flattened, previousTabID: "missing") == 1)
	}

	@Test("falls back to 0 when only one tab is present")
	func singleTab() {
		#expect(TabSwitcher.initialSelectedIndex([SwitcherTab(id: "only", title: "only")], previousTabID: nil) == 0)
	}

	@Test("returns 0 for an empty list")
	func emptyList() {
		#expect(TabSwitcher.initialSelectedIndex([], previousTabID: nil) == 0)
	}
}

@Suite("TabSwitcher.groupOffsets")
struct GroupOffsetsTests {
	@Test("returns the running tab-count offset for each group")
	func runningOffsets() {
		let groups = TabSwitcher.groups(mru: ["w1", "w2", "p1"], profiles: profiles, maxTabs: 8)

		#expect(groups.map(\.tabs.count) == [2, 1])
		#expect(TabSwitcher.groupOffsets(groups) == [0, 2])
	}

	@Test("returns an empty array for no groups")
	func noGroups() {
		#expect(TabSwitcher.groupOffsets([]) == [])
	}
}

@Suite("TabSwitcher.cycle")
struct CycleTests {
	@Test("advances within range")
	func advances() {
		#expect(TabSwitcher.cycle(0, by: 1, count: 3) == 1)
	}

	@Test("wraps forward past the last index")
	func wrapsForward() {
		#expect(TabSwitcher.cycle(2, by: 1, count: 3) == 0)
	}

	@Test("wraps backward past the first index")
	func wrapsBackward() {
		#expect(TabSwitcher.cycle(0, by: -1, count: 3) == 2)
	}

	@Test("returns 0 for an empty list")
	func emptyList() {
		#expect(TabSwitcher.cycle(0, by: 1, count: 0) == 0)
	}
}
