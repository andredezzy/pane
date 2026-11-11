// Pastes one image over another at a point, both in pixels, and writes the
// result: Pane's window as the channel renders it, with a page's own capture
// in the page's place, as Paper's 10.1 to 10.3 were made.
// Usage: image-paste <base.png> <overlay.png> <x> <y> <out.png>
import AppKit

let arguments = CommandLine.arguments

guard
	arguments.count == 6,
	let base = NSImage(contentsOfFile: arguments[1])?
		.cgImage(forProposedRect: nil, context: nil, hints: nil),
	let overlay = NSImage(contentsOfFile: arguments[2])?
		.cgImage(forProposedRect: nil, context: nil, hints: nil),
	let x = Int(arguments[3]),
	let y = Int(arguments[4]),
	let context = CGContext(
		data: nil,
		width: base.width,
		height: base.height,
		bitsPerComponent: 8,
		bytesPerRow: 0,
		space: base.colorSpace ?? CGColorSpace(name: CGColorSpace.sRGB)!,
		bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
	)
else {
	print("{\"error\":\"usage: image-paste <base.png> <overlay.png> <x> <y> <out.png>\"}")
	exit(1)
}

// Core Graphics counts up from the bottom.
context.draw(base, in: CGRect(x: 0, y: 0, width: base.width, height: base.height))
context.draw(
	overlay,
	in: CGRect(
		x: x,
		y: base.height - y - overlay.height,
		width: overlay.width,
		height: overlay.height
	)
)

guard
	let result = context.makeImage(),
	let png = NSBitmapImageRep(cgImage: result).representation(using: .png, properties: [:])
else {
	print("{\"error\":\"could not write the image\"}")
	exit(1)
}

try png.write(to: URL(fileURLWithPath: arguments[5]))
print("{\"width\":\(result.width),\"height\":\(result.height)}")
