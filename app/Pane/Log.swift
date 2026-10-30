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
}
