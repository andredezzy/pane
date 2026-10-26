// Prints, as JSON, how much detail a region of an image has: the standard
// deviation of its luminance (0 for a flat color; text and images raise it).
// Usage: image-detail <png> <x0> <y0> <x1> <y1>   (fractions of the size)
import AppKit

let arguments = CommandLine.arguments

guard
	arguments.count == 6,
	let image = NSImage(contentsOfFile: arguments[1]),
	let cgImage = image.cgImage(forProposedRect: nil, context: nil, hints: nil)
else {
	print("{\"error\":\"usage: image-detail <png> <x0> <y0> <x1> <y1>\"}")
	exit(1)
}

let bitmap = NSBitmapImageRep(cgImage: cgImage)
let fractions = arguments[2...5].compactMap(Double.init)
let width = Double(bitmap.pixelsWide)
let height = Double(bitmap.pixelsHigh)
let xs = Int(fractions[0] * width) ..< Int(fractions[2] * width)
let ys = Int(fractions[1] * height) ..< Int(fractions[3] * height)

var sum = 0.0
var squares = 0.0
var count = 0.0

for y in stride(from: ys.lowerBound, to: ys.upperBound, by: 2) {
	for x in stride(from: xs.lowerBound, to: xs.upperBound, by: 2) {
		guard let color = bitmap.colorAt(x: x, y: y)?.usingColorSpace(.sRGB) else {
			continue
		}

		let luminance = 0.2126 * color.redComponent + 0.7152 * color.greenComponent + 0.0722 * color.blueComponent
		sum += luminance
		squares += luminance * luminance
		count += 1
	}
}

let mean = sum / max(count, 1)
let deviation = sqrt(max(squares / max(count, 1) - mean * mean, 0))
print("{\"mean\":\(mean),\"deviation\":\(deviation),\"samples\":\(Int(count))}")
