// Waits for the first on-screen, normal-layer window owned by a process, and
// prints, as JSON, when the window list first showed it (milliseconds since
// 1970) and when the list before it was read, which bounds the moment.
// Usage: window-wait <pid> [timeout in ms, 60000]
import CoreGraphics
import Foundation

let arguments = CommandLine.arguments

guard arguments.count >= 2, let pid = Int32(arguments[1]) else {
	FileHandle.standardError.write(Data("usage: window-wait <pid> [timeout ms]\n".utf8))
	exit(2)
}

let timeout = (arguments.count > 2 ? Double(arguments[2]) : nil) ?? 60_000
let deadline = Date().addingTimeInterval(timeout / 1000)
let options: CGWindowListOption = [.optionOnScreenOnly, .excludeDesktopElements]
var previous: Double?
var polls = 0

func milliseconds() -> Double {
	Date().timeIntervalSince1970 * 1000
}

func report(_ output: [String: Any]) {
	let data = try? JSONSerialization.data(withJSONObject: output, options: [.sortedKeys])
	print(String(decoding: data ?? Data(), as: UTF8.self))
}

while Date() < deadline {
	polls += 1
	let all = CGWindowListCopyWindowInfo(options, kCGNullWindowID) as? [[String: Any]] ?? []
	let read = milliseconds()

	let window = all.first { info in
		info[kCGWindowOwnerPID as String] as? Int32 == pid && info[kCGWindowLayer as String] as? Int == 0
	}

	if let window {
		report([
			"seenAt": read,
			"notSeenAt": previous ?? NSNull(),
			"polls": polls,
			"bounds": window[kCGWindowBounds as String] as? [String: Double] ?? [:],
		])
		exit(0)
	}

	previous = read
	usleep(2_000)
}

report(["seenAt": NSNull(), "notSeenAt": previous ?? NSNull(), "polls": polls, "bounds": [:]])
exit(1)
