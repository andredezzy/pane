// Prints, as JSON, the on-screen normal-layer windows owned by the given PIDs;
// with --menus, their windows on the system's pop-up menu layer instead (an
// open NSMenu), each with the screen its top left is on: the whole screen,
// and the part the menu bar and the Dock leave. Bounds are in points from the
// top left of the main screen.
// Usage: windows [--menus] <pid>...
import AppKit
import CoreGraphics
import Foundation

let arguments = Array(CommandLine.arguments.dropFirst())
let menus = arguments.first == "--menus"
let pids = Set(arguments.dropFirst(menus ? 1 : 0).compactMap { Int32($0) })
let wanted = menus ? Int(CGWindowLevelForKey(.popUpMenuWindow)) : 0
let options: CGWindowListOption = [.optionOnScreenOnly, .excludeDesktopElements]
let all = CGWindowListCopyWindowInfo(options, kCGNullWindowID) as? [[String: Any]] ?? []

// A rect of AppKit's, from the main screen's bottom left, as a window's
// bounds are written.
func written(_ rect: NSRect) -> [String: Double] {
	let main = NSScreen.screens.first?.frame.maxY ?? 0

	return ["X": rect.minX, "Y": main - rect.maxY, "Width": rect.width, "Height": rect.height]
}

// The screen at `point` (from the main screen's top left): its whole frame,
// and the part the menu bar and the Dock leave.
func screen(at point: CGPoint) -> [String: Any] {
	let main = NSScreen.screens.first?.frame.maxY ?? 0
	let flipped = NSPoint(x: point.x, y: main - point.y)
	guard let screen = NSScreen.screens.first(where: { NSPointInRect(flipped, $0.frame) }) else {
		return [:]
	}

	return ["frame": written(screen.frame), "visible": written(screen.visibleFrame)]
}

let windows: [[String: Any]] = all.compactMap { info in
	guard
		let pid = info[kCGWindowOwnerPID as String] as? Int32, pids.contains(pid),
		let layer = info[kCGWindowLayer as String] as? Int, layer == wanted
	else {
		return nil
	}

	let bounds = info[kCGWindowBounds as String] as? [String: Double] ?? [:]
	var window: [String: Any] = [
		"pid": pid,
		"owner": info[kCGWindowOwnerName as String] as? String ?? "",
		"title": info[kCGWindowName as String] as? String ?? "",
		"bounds": bounds,
	]

	if menus {
		window["screen"] = screen(at: CGPoint(x: bounds["X"] ?? 0, y: bounds["Y"] ?? 0))
	}

	return window
}

let output: [String: Any] = ["count": windows.count, "windows": windows]
let data = try JSONSerialization.data(withJSONObject: output, options: [.prettyPrinted, .sortedKeys])
print(String(decoding: data, as: UTF8.self))
