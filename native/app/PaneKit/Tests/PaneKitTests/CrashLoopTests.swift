import Foundation
import PaneKit
import Testing

@Suite("crash loop")
struct CrashLoopTests {
	private let start = Date(timeIntervalSince1970: 1_800_000_000)

	// Whether each crash, `seconds` after the start, is a loop.
	private func loops(_ crashes: [(tab: String, seconds: TimeInterval)], forgetting forgotten: String? = nil, after index: Int = -1) -> [Bool] {
		var loop = CrashLoop()

		return crashes.enumerated().map { position, crash in
			defer {
				if position == index, let forgotten {
					loop.forget(forgotten)
				}
			}

			return loop.crashed(crash.tab, at: start.addingTimeInterval(crash.seconds))
		}
	}

	@Test("a first crash reloads; a second within a minute is a loop")
	func secondWithinMinute() {
		// Another tab's crash is its own.
		#expect(loops([("a", 0), ("a", 59), ("b", 59)]) == [false, true, false])
	}

	@Test("a crash a minute or more after the last reloads again")
	func afterMinute() {
		#expect(loops([("a", 0), ("a", 60), ("a", 61)]) == [false, false, true])
	}

	@Test("a forgotten tab starts over")
	func forgets() {
		#expect(loops([("a", 0), ("a", 1)], forgetting: "a", after: 0) == [false, false])
	}
}
