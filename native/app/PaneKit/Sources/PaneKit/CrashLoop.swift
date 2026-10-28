import Foundation

// When each tab's renderer last died, to tell a crash loop from a crash. The
// first crash reloads the page, as a slept tab wakes; a page that dies again
// within a minute shows the sad tab instead of looping.
public struct CrashLoop: Sendable {
	public static let window: TimeInterval = 60

	private var lastCrash: [String: Date] = [:]

	public init() {}

	// Records the crash; whether it is a second one within the window.
	public mutating func crashed(_ tabID: String, at now: Date) -> Bool {
		let looping = lastCrash[tabID].map { now.timeIntervalSince($0) < Self.window } ?? false
		lastCrash[tabID] = now

		return looping
	}

	// A tab reloaded from its sad tab, released, or closed starts over.
	public mutating func forget(_ tabID: String) {
		lastCrash[tabID] = nil
	}
}
