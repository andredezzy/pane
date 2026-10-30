// Finder's and Paper's icons, as macOS draws them, at 1024 pixels: the dock's
// neighbours on either side of Pane.
// Usage: swift app-icons.swift <build dir>   → finder.png, paper.png
import AppKit

let build = URL(fileURLWithPath: CommandLine.arguments[1])
for (app, name) in [("/System/Library/CoreServices/Finder.app", "finder"), ("/Applications/Paper.app", "paper")] {
	precondition(FileManager.default.fileExists(atPath: app), "\(app) is not installed")
	let icon = NSWorkspace.shared.icon(forFile: app)
	let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: 1024, pixelsHigh: 1024, bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true,
	                           isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
	NSGraphicsContext.saveGraphicsState()
	NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
	icon.draw(in: NSRect(x: 0, y: 0, width: 1024, height: 1024), from: .zero, operation: .copy, fraction: 1, respectFlipped: false,
	          hints: [.interpolation: NSImageInterpolation.high])
	NSGraphicsContext.restoreGraphicsState()
	try! rep.representation(using: .png, properties: [:])!.write(to: build.appendingPathComponent("\(name).png"))
}
