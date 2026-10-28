// The Ctrl+Tab switcher's model: the most recently used tabs, grouped under
// their profile. Group order and tab order within a group both follow
// recency; the total is capped so the switcher stays a recent-tabs list.

public struct SwitcherTab: Hashable, Identifiable, Sendable {
	public var id: String
	public var title: String
	public var favicon: String?

	public init(id: String, title: String, favicon: String? = nil) {
		self.id = id
		self.title = title
		self.favicon = favicon
	}
}

public struct SwitcherGroup: Hashable, Identifiable, Sendable {
	public var id: String
	public var name: String
	public var color: ProfileColor
	public var tabs: [SwitcherTab]
}

public enum TabSwitcher {
	public static let maxVisibleTabs = 8

	public static func groups(mru: [String], profiles: [BrowserProfile], maxTabs: Int = maxVisibleTabs) -> [SwitcherGroup] {
		var profileByTabID: [String: BrowserProfile] = [:]

		for profile in profiles {
			for tab in profile.tabs {
				profileByTabID[tab.id] = profile
			}
		}

		var groups: [SwitcherGroup] = []
		var indexByProfileID: [String: Int] = [:]
		var count = 0

		for tabID in mru where count < maxTabs {
			guard
				let profile = profileByTabID[tabID],
				let tab = profile.tabs.first(where: { $0.id == tabID })
			else {
				continue
			}

			let index = indexByProfileID[profile.id] ?? {
				groups.append(SwitcherGroup(id: profile.id, name: profile.name, color: profile.color, tabs: []))
				indexByProfileID[profile.id] = groups.count - 1

				return groups.count - 1
			}()

			groups[index].tabs.append(SwitcherTab(
				id: tab.id,
				title: tab.title.isEmpty ? "Loading..." : tab.title,
				favicon: tab.favicon.isEmpty ? nil : tab.favicon
			))
			count += 1
		}

		return groups
	}

	// Where selection starts: the previous tab, so a quick flick of Ctrl+Tab
	// goes back to it; else the second item.
	public static func initialSelectedIndex(_ flattened: [SwitcherTab], previousTabID: String?) -> Int {
		if flattened.isEmpty {
			return 0
		}

		return flattened.firstIndex { $0.id == previousTabID } ?? min(1, flattened.count - 1)
	}

	// Each group's offset into the flattened tab list.
	public static func groupOffsets(_ groups: [SwitcherGroup]) -> [Int] {
		var offsets: [Int] = []
		var running = 0

		for group in groups {
			offsets.append(running)
			running += group.tabs.count
		}

		return offsets
	}

	public static func cycle(_ current: Int, by delta: Int, count: Int) -> Int {
		count == 0 ? 0 : (current + delta + count) % count
	}
}
