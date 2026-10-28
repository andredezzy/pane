import PaneKit
import Testing

@Suite("reordering a list by dragging")
struct ReorderTests {
	// Four collapsed profiles: 36 tall, 4 apart, from 0.
	private let ids = ["a", "b", "c", "d"]
	private let heights = ["a": 36.0, "b": 36, "c": 36, "d": 36]

	private func target(_ dragged: String, top: Double, heights: [String: Double]? = nil) -> Int {
		Reorder.target(ids, heights: heights ?? self.heights, firstTop: 0, dragged: dragged, top: top, spacing: 4)
	}

	@Test("drops at the place whose top is nearest the copy's")
	func nearestPlace() {
		#expect(target("a", top: 0) == 0)
		#expect(target("a", top: 19) == 0)
		#expect(target("a", top: 21) == 1)
		#expect(target("a", top: 86) == 2)
		#expect(target("a", top: 120) == 3)
		#expect(target("d", top: 50) == 1)
	}

	@Test("drops first or last past the list's ends")
	func ends() {
		#expect(target("d", top: -400) == 0)
		#expect(target("a", top: 4_000) == 3)
	}

	@Test("counts an expanded profile's whole height")
	func expanded() {
		// b expanded: its header and three tabs, 36 + 3 × 36.
		let tall = ["a": 36.0, "b": 144, "c": 36, "d": 36]

		// With a out, the places' tops are 0, 148 (after b), and 188
		// (after c).
		#expect(target("a", top: 140, heights: tall) == 1)
		#expect(target("a", top: 160, heights: tall) == 1)
		#expect(target("a", top: 170, heights: tall) == 2)
	}

	@Test("moves a row to its target among the others")
	func order() {
		#expect(Reorder.order(ids, moving: "a", to: 2) == ["b", "c", "a", "d"])
		#expect(Reorder.order(ids, moving: "d", to: 0) == ["d", "a", "b", "c"])
		#expect(Reorder.order(ids, moving: "b", to: 9) == ["a", "c", "d", "b"])
		#expect(Reorder.order(ids, moving: "z", to: 0) == ids)
	}
}
