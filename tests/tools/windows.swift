// Prints, as JSON, the on-screen normal-layer windows owned by the given PIDs.
// Usage: windows <pid>...
import CoreGraphics
import Foundation

let pids = Set(CommandLine.arguments.dropFirst().compactMap { Int32($0) })
let options: CGWindowListOption = [.optionOnScreenOnly, .excludeDesktopElements]
let all = CGWindowListCopyWindowInfo(options, kCGNullWindowID) as? [[String: Any]] ?? []

let windows: [[String: Any]] = all.compactMap { info in
	guard
		let pid = info[kCGWindowOwnerPID as String] as? Int32, pids.contains(pid),
		let layer = info[kCGWindowLayer as String] as? Int, layer == 0
	else {
		return nil
	}

	return [
		"pid": pid,
		"owner": info[kCGWindowOwnerName as String] as? String ?? "",
		"title": info[kCGWindowName as String] as? String ?? "",
		"bounds": info[kCGWindowBounds as String] as? [String: Double] ?? [:],
	]
}

let output: [String: Any] = ["count": windows.count, "windows": windows]
let data = try JSONSerialization.data(withJSONObject: output, options: [.prettyPrinted, .sortedKeys])
print(String(decoding: data, as: UTF8.self))
