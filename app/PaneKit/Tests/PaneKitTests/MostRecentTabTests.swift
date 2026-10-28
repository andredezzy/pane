// Ported from apps/veil/src/main/profile/mru.test.ts.
import PaneKit
import Testing

private struct Item: Identifiable, Equatable {
	let id: String
}

private let tabs = [Item(id: "a"), Item(id: "b"), Item(id: "c")]

@Suite("mostRecentTab")
struct MostRecentTabTests {
	@Test("returns the most recently used tab still present")
	func mostRecentPresent() {
		// b is more recent than c in the history, and both still exist.
		#expect(mostRecentTab(tabs, history: ["x", "b", "c", "a"])?.id == "b")
	}

	@Test("skips history ids that are no longer present")
	func skipsGone() {
		#expect(mostRecentTab(tabs, history: ["gone", "c", "a"])?.id == "c")
	}

	@Test("falls back to the last tab when none are in the history")
	func fallsBackToLast() {
		#expect(mostRecentTab(tabs, history: [])?.id == "c")
		#expect(mostRecentTab(tabs, history: ["nope"])?.id == "c")
	}

	@Test("returns nil when there are no tabs")
	func emptyTabs() {
		#expect(mostRecentTab([Item](), history: ["a"]) == nil)
	}
}
