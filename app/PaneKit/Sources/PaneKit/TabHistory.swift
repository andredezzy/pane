// Which tabs were used most recently, across every profile, and which were
// closed (for Reopen closed tab). Session state: not persisted, like the
// Electron app's tab store.
public struct TabHistory: Hashable, Sendable {
	public static let maxClosedTabs = 20

	// Most recent first.
	public private(set) var mru: [String] = []
	// Most recently closed first, at most `maxClosedTabs`.
	public private(set) var closed: [ClosedTab] = []

	public init() {}

	public mutating func use(_ tabID: String) {
		mru.removeAll { $0 == tabID }
		mru.insert(tabID, at: 0)
	}

	public mutating func forget(_ tabID: String) {
		mru.removeAll { $0 == tabID }
	}

	public mutating func pushClosed(_ tab: ClosedTab) {
		closed.insert(tab, at: 0)
		closed = Array(closed.prefix(Self.maxClosedTabs))
	}

	public mutating func popClosed() -> ClosedTab? {
		closed.isEmpty ? nil : closed.removeFirst()
	}
}

public struct ClosedTab: Hashable, Sendable {
	public var url: String
	public var profileID: String
	public var title: String?
	public var favicon: String?

	public init(url: String, profileID: String, title: String? = nil, favicon: String? = nil) {
		self.url = url
		self.profileID = profileID
		self.title = title
		self.favicon = favicon
	}
}

// The most recently used tab still present in `tabs`, falling back to the last
// one when none of them appear in the history.
public func mostRecentTab<T: Identifiable>(_ tabs: [T], history: [String]) -> T? where T.ID == String {
	for tabID in history {
		if let tab = tabs.first(where: { $0.id == tabID }) {
			return tab
		}
	}

	return tabs.last
}
