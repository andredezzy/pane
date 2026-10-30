import Foundation
import os

// Pane's channels in the unified log, by area: what failed and why. A path,
// an error's text (which can carry a URL), or a page's address is private,
// so the log shows it only on this Mac while a profile of Apple's asks for
// it; IDs, counts, and timings are not.
// `log show --predicate 'subsystem == "com.pane.desktop"'` reads them.
enum Log {
	private static let subsystem = "com.pane.desktop"

	static let app = Logger(subsystem: subsystem, category: "app")
	static let engine = Logger(subsystem: subsystem, category: "engine")
	static let migration = Logger(subsystem: subsystem, category: "migration")
	static let model = Logger(subsystem: subsystem, category: "model")
	static let network = Logger(subsystem: subsystem, category: "network")
	static let security = Logger(subsystem: subsystem, category: "security")
	static let updates = Logger(subsystem: subsystem, category: "updates")

	// From an input to the frame it changes, in Instruments' Points of
	// Interest, or `log stream --signpost`: an interval begins as the input
	// is handled and ends once Core Animation has committed the frame built
	// in its turn to the screen's server (AfterFrame).
	private static let interactions = OSSignposter(subsystem: subsystem, category: .pointsOfInterest)

	@MainActor
	static func interaction(_ name: StaticString) {
		let state = interactions.beginInterval(name, id: interactions.makeSignpostID())

		AfterFrame.committed {
			interactions.endInterval(name, state)
		}
	}
}
