// Draws the dmg window's background, as Hark's installer lays it out (Paper:
// 08 · Installer › 08.1 Installer window): a hazy sky, blue at the top and
// pale gray at the bottom, "Install Pane" above in the wordmark's type
// (Funnel Display, ADR 0099), "Drag the app onto the Applications folder"
// under it in the system font, as Finder's labels are, and a hand-drawn
// arrow between where Finder draws Pane and the Applications link (dist.sh
// puts them there). Writes background.tiff beside this file, at 1x and 2x.
// Usage: swift app/dmg/make-background.swift
import AppKit

// The window shows 720 by 560 points under Finder's title bar (dist.sh). The
// image runs 40 points taller, the sky's last colour alone, so a shorter
// title bar leaves no gap.
let visible = NSSize(width: 720, height: 560)
let size = NSSize(width: 720, height: 600)
let directory = URL(fileURLWithPath: #filePath).deletingLastPathComponent()

// The wordmark's type, registered for this process alone.
CTFontManagerRegisterFontsForURL(directory.appending(path: "fonts/FunnelDisplay[wght].ttf") as CFURL, .process, nil)

func funnelDisplay(size: CGFloat, weight: CGFloat) -> NSFont {
	let wght = 0x7767_6874 // 'wght'
	let descriptor = CTFontDescriptorCreateWithAttributes([
		kCTFontFamilyNameAttribute: "Funnel Display",
		kCTFontVariationAttribute: [wght: weight],
	] as CFDictionary)
	let font = CTFontCreateWithFontDescriptor(descriptor, size, nil)
	precondition(CTFontCopyFamilyName(font) as String == "Funnel Display", "Funnel Display did not load")
	return font as NSFont
}

func components(_ rgb: Int) -> (CGFloat, CGFloat, CGFloat) {
	(CGFloat((rgb >> 16) & 0xFF) / 255, CGFloat((rgb >> 8) & 0xFF) / 255, CGFloat(rgb & 0xFF) / 255)
}

func color(_ rgb: Int, _ alpha: CGFloat = 1) -> CGColor {
	let (r, g, b) = components(rgb)
	return CGColor(srgbRed: r, green: g, blue: b, alpha: alpha)
}

// Paper mixes its gradients in Oklab, so the sky's stops are mixed there too
// and handed to Core Graphics finely enough that its sRGB mixing between
// them does not show.
func oklab(_ rgb: Int) -> SIMD3<Double> {
	let (r, g, b) = components(rgb)
	let linear = [r, g, b].map { c -> Double in
		let c = Double(c)
		return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4)
	}
	let l = cbrt(0.4122214708 * linear[0] + 0.5363325363 * linear[1] + 0.0514459929 * linear[2])
	let m = cbrt(0.2119034982 * linear[0] + 0.6806995451 * linear[1] + 0.1073969566 * linear[2])
	let s = cbrt(0.0883024619 * linear[0] + 0.2817188376 * linear[1] + 0.6299787005 * linear[2])
	return [
		0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
		1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
		0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s,
	]
}

func srgb(_ lab: SIMD3<Double>) -> CGColor {
	let l = pow(lab.x + 0.3963377774 * lab.y + 0.2158037573 * lab.z, 3)
	let m = pow(lab.x - 0.1055613458 * lab.y - 0.0638541728 * lab.z, 3)
	let s = pow(lab.x - 0.0894841775 * lab.y - 1.2914855480 * lab.z, 3)
	let linear = [
		4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
		-1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
		-0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s,
	]
	let c = linear.map { c -> CGFloat in
		let c = min(max(c, 0), 1)
		return CGFloat(c <= 0.0031308 ? 12.92 * c : 1.055 * pow(c, 1 / 2.4) - 0.055)
	}
	return CGColor(srgbRed: c[0], green: c[1], blue: c[2], alpha: 1)
}

func oklabGradient(_ stops: [(Int, CGFloat)]) -> CGGradient {
	var colors: [CGColor] = [], locations: [CGFloat] = []
	for (index, (rgb, location)) in stops.enumerated().dropLast() {
		let (next, end) = stops[index + 1]
		for step in 0..<16 {
			let t = Double(step) / 16
			colors.append(srgb(oklab(rgb) * (1 - t) + oklab(next) * t))
			locations.append(location + (end - location) * CGFloat(t))
		}
	}
	colors.append(color(stops[stops.count - 1].0))
	locations.append(stops[stops.count - 1].1)
	return CGGradient(colorsSpace: CGColorSpace(name: CGColorSpace.sRGB), colors: colors as CFArray, locations: locations)!
}

// The sky, top to bottom, as Hark's measures.
let sky = oklabGradient([
	(0x8FB5D8, 0), (0x9BBEDE, 0.10), (0xA8C8E4, 0.18), (0xB4CFE6, 0.26), (0xC0D7EA, 0.34),
	(0xC9DDEC, 0.42), (0xD3E2EC, 0.58), (0xDBE4EA, 0.74), (0xE1E6E9, 0.90), (0xE4E6E8, 1),
])

// Its haze: a CSS `radial-gradient(ellipse <rx> <ry> at <x> <y>, <color>
// <alpha> 0%, transparent 100%)`, each a fraction of the visible sky. The
// first lies on top, so they are drawn last to first.
let haze: [(rx: CGFloat, ry: CGFloat, x: CGFloat, y: CGFloat, rgb: Int, alpha: CGFloat)] = [
	(0.30, 0.09, 0.14, 0.16, 0xFFFFFF, 0x38),
	(0.34, 0.08, 0.78, 0.24, 0xFFFFFF, 0x30),
	(0.46, 0.14, 0.22, 0.34, 0xFFFFFF, 0x33),
	(0.40, 0.12, 0.86, 0.44, 0xFFFFFF, 0x33),
	(0.28, 0.08, 0.04, 0.52, 0xFFFFFF, 0x2E),
	(0.60, 0.16, 0.46, 0.64, 0xFFFFFF, 0x2B),
	(0.44, 0.12, 0.64, 0.06, 0x7BA4CF, 0x33),
	(0.36, 0.12, 0.06, 0.04, 0x7BA4CF, 0x2E),
]

// One line of text, centred across the window on a CSS line box `top`
// points down and `height` tall, its baseline where a browser puts it.
func line(_ text: NSAttributedString, top: CGFloat, height: CGFloat, in context: CGContext) {
	let line = CTLineCreateWithAttributedString(text)
	var ascent: CGFloat = 0, descent: CGFloat = 0
	CTLineGetTypographicBounds(line, &ascent, &descent, nil)
	// A tracked line's last letter carries its tracking too; CSS does the
	// same, so the width is centred as it stands.
	let width = CTLineGetTypographicBounds(line, nil, nil, nil)
	let baseline = top + (height - ascent - descent) / 2 + ascent
	context.saveGState()
	context.textMatrix = CGAffineTransform(scaleX: 1, y: -1)
	context.textPosition = CGPoint(x: (visible.width - width) / 2, y: baseline)
	CTLineDraw(line, context)
	context.restoreGState()
}

func draw(_ context: CGContext) {
	context.drawLinearGradient(sky, start: .zero, end: CGPoint(x: 0, y: visible.height), options: [.drawsAfterEndLocation])

	for cloud in haze.reversed() {
		let tint = color(cloud.rgb, cloud.alpha / 255), clear = color(cloud.rgb, 0)
		let gradient = CGGradient(colorsSpace: CGColorSpace(name: CGColorSpace.sRGB), colors: [tint, clear] as CFArray, locations: [0, 1])!
		context.saveGState()
		context.translateBy(x: cloud.x * visible.width, y: cloud.y * visible.height)
		context.scaleBy(x: cloud.rx * visible.width, y: cloud.ry * visible.height)
		context.drawRadialGradient(gradient, startCenter: .zero, startRadius: 0, endCenter: .zero, endRadius: 1, options: [])
		context.restoreGState()
	}

	let ink = NSColor(cgColor: color(0x2A2D33))!
	let headline = NSMutableAttributedString(string: "Install ", attributes: [.font: funnelDisplay(size: 32, weight: 400), .foregroundColor: ink, .kern: -0.64])
	headline.append(NSAttributedString(string: "Pane", attributes: [.font: funnelDisplay(size: 32, weight: 500), .foregroundColor: ink, .kern: -0.64]))
	line(headline, top: 82, height: 42, in: context)

	let subtitle = NSAttributedString(string: "Drag the app onto the Applications folder", attributes: [
		.font: NSFont.systemFont(ofSize: 18), .foregroundColor: NSColor(cgColor: color(0x8494A6))!, .kern: -0.18,
	])
	line(subtitle, top: 129, height: 22, in: context)

	// The hand-drawn arrow (08.2 Arrow options, A10), on the icons' centre
	// line: its 60 by 44 box 336 points in and 262 down.
	let arrow = CGMutablePath()
	arrow.move(to: CGPoint(x: 4, y: 25))
	arrow.addCurve(to: CGPoint(x: 52, y: 21), control1: CGPoint(x: 15, y: 21), control2: CGPoint(x: 29, y: 26))
	arrow.move(to: CGPoint(x: 39, y: 9))
	arrow.addCurve(to: CGPoint(x: 53, y: 21), control1: CGPoint(x: 44, y: 14), control2: CGPoint(x: 48, y: 17))
	arrow.addCurve(to: CGPoint(x: 41, y: 34), control1: CGPoint(x: 47, y: 24), control2: CGPoint(x: 43, y: 28))
	context.saveGState()
	context.translateBy(x: 336, y: 262)
	context.addPath(arrow)
	context.setStrokeColor(color(0x2A2D33))
	context.setLineWidth(3.5)
	context.setLineCap(.round)
	context.setLineJoin(.round)
	context.strokePath()
	context.restoreGState()
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
	// inch, and Finder takes it as the same 720 by 600 points.
	rep.size = size

	NSGraphicsContext.saveGraphicsState()
	let context = NSGraphicsContext(bitmapImageRep: rep)!
	// Top-left origin, as the design measures; the context already maps
	// points to the rep's pixels.
	context.cgContext.translateBy(x: 0, y: size.height)
	context.cgContext.scaleBy(x: 1, y: -1)
	NSGraphicsContext.current = NSGraphicsContext(cgContext: context.cgContext, flipped: true)
	draw(context.cgContext)
	NSGraphicsContext.restoreGraphicsState()

	// Tagged sRGB, as its colours are, so no display takes them for its own.
	return rep.retagging(with: .sRGB)!
}

let output = directory.appendingPathComponent("background.tiff")
let tiff = NSBitmapImageRep.tiffRepresentationOfImageReps(in: [rep(scale: 1), rep(scale: 2)], using: .lzw, factor: 0)!
try tiff.write(to: output)
print("wrote \(output.path)")
