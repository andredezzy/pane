// Draws the dmg window's background, as Dia's installer lays it out (Paper:
// 08 · Installer › 08.1 Installer window): a neutral wash, white at the top
// and gray at the corners, "To install, drag Pane to Applications" above, and
// three chevrons, light to dark, between where Finder draws Pane and the
// Applications link (dist.sh puts them there). The headline is in the system
// font, as Finder's labels are. Writes background.tiff beside this file, at
// 1x and 2x.
// Usage: swift app/dmg/make-background.swift
import AppKit

// The window shows 800 by 380 points under Finder's title bar (dist.sh). The
// image runs 40 points taller, the wash alone, so a shorter title bar leaves
// no gap.
let visible = NSSize(width: 800, height: 380)
let size = NSSize(width: 800, height: 420)
let directory = URL(fileURLWithPath: #filePath).deletingLastPathComponent()

func color(_ rgb: Int, _ alpha: CGFloat = 1) -> NSColor {
	NSColor(srgbRed: CGFloat((rgb >> 16) & 0xFF) / 255, green: CGFloat((rgb >> 8) & 0xFF) / 255, blue: CGFloat(rgb & 0xFF) / 255, alpha: alpha)
}

// A CSS `radial-gradient(circle farthest-corner at <center>, <color> 0%,
// transparent <stop>)`.
func glow(at center: CGPoint, _ rgb: Int, alpha: CGFloat, stop: CGFloat) {
	let corners = [CGPoint.zero, CGPoint(x: visible.width, y: 0), CGPoint(x: 0, y: visible.height), CGPoint(x: visible.width, y: visible.height)]
	let farthest = corners.map { hypot($0.x - center.x, $0.y - center.y) }.max() ?? 0
	NSGradient(starting: color(rgb, alpha), ending: color(rgb, 0))?
		.draw(fromCenter: center, radius: 0, toCenter: center, radius: farthest * stop, options: [])
}

func draw() {
	let bounds = NSRect(origin: .zero, size: size)

	color(0xEEEEF0).setFill()
	bounds.fill()
	NSGradient(colors: [color(0xFAFAFA), color(0xF5F5F6), color(0xEEEEF0)], atLocations: [0, 0.55, 1], colorSpace: .sRGB)?
		.draw(in: NSRect(origin: .zero, size: visible), angle: 90)

	glow(at: CGPoint(x: visible.width / 2, y: 0), 0xFFFFFF, alpha: 1, stop: 0.40)
	glow(at: .zero, 0xD4D4D8, alpha: 0.65, stop: 0.34)
	glow(at: CGPoint(x: visible.width, y: 0), 0xD4D4D8, alpha: 0.65, stop: 0.34)
	glow(at: CGPoint(x: 0, y: visible.height), 0xA1A1AA, alpha: 0.28, stop: 0.36)
	glow(at: CGPoint(x: visible.width, y: visible.height), 0xA1A1AA, alpha: 0.28, stop: 0.36)

	let regular: [NSAttributedString.Key: Any] = [.font: NSFont.systemFont(ofSize: 18), .foregroundColor: color(0x71717A)]
	let strong: [NSAttributedString.Key: Any] = [.font: NSFont.systemFont(ofSize: 18, weight: .semibold), .foregroundColor: color(0x18181B)]
	let headline = NSMutableAttributedString()

	for (text, attributes) in [("To install, drag ", regular), ("Pane", strong), (" to ", regular), ("Applications", strong)] {
		headline.append(NSAttributedString(string: text, attributes: attributes))
	}

	// Centered on a 24-point line whose top is 44 points down.
	let line = headline.size()
	headline.draw(at: NSPoint(x: (visible.width - line.width) / 2, y: 44 + (24 - line.height) / 2))

	// The chevrons, on the icons' center line (186 points down).
	for (index, rgb) in [0xD4D4D8, 0xA1A1AA, 0x18181B].enumerated() {
		let x = 376 + CGFloat(index) * 18
		let chevron = NSBezierPath()
		chevron.move(to: NSPoint(x: x, y: 176))
		chevron.line(to: NSPoint(x: x + 10, y: 186))
		chevron.line(to: NSPoint(x: x, y: 196))
		chevron.lineWidth = 3.5
		chevron.lineCapStyle = .round
		chevron.lineJoinStyle = .round
		color(rgb).setStroke()
		chevron.stroke()
	}
}

// One image of the background, `scale` pixels to the point.
func rep(scale: CGFloat) -> NSBitmapImageRep {
	let rep = NSBitmapImageRep(
		bitmapDataPlanes: nil,
		pixelsWide: Int(size.width * scale),
		pixelsHigh: Int(size.height * scale),
		bitsPerSample: 8,
		samplesPerPixel: 4,
		hasAlpha: true,
		isPlanar: false,
		colorSpaceName: .deviceRGB,
		bytesPerRow: 0,
		bitsPerPixel: 0
	)!
	// Points, not pixels: the TIFF then records the 2x image at 144 dots per
	// inch, and Finder takes it as the same 800 by 380 points.
	rep.size = size

	NSGraphicsContext.saveGraphicsState()
	let context = NSGraphicsContext(bitmapImageRep: rep)!
	// Top-left origin, as the design measures; the context already maps
	// points to the rep's pixels.
	context.cgContext.translateBy(x: 0, y: size.height)
	context.cgContext.scaleBy(x: 1, y: -1)
	NSGraphicsContext.current = NSGraphicsContext(cgContext: context.cgContext, flipped: true)
	draw()
	NSGraphicsContext.restoreGraphicsState()

	return rep
}

let output = directory.appendingPathComponent("background.tiff")
let tiff = NSBitmapImageRep.tiffRepresentationOfImageReps(in: [rep(scale: 1), rep(scale: 2)], using: .lzw, factor: 0)!
try tiff.write(to: output)
print("wrote \(output.path)")
