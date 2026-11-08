// Compares two images of one size: prints the share of pixels whose red,
// green, or blue differs by more than 2/255. Given a third path, writes there
// a PNG of the first image dimmed, with each differing pixel in red. Exits 1
// when the sizes differ, 2 when an image can't be read or the PNG written.
// Usage: image-diff <a.png> <b.png> [diff.png]
import CoreGraphics
import Foundation
import ImageIO

typealias Pixels = (width: Int, height: Int, rgba: [UInt8])

let space = CGColorSpaceCreateDeviceRGB()
let premultiplied = CGImageAlphaInfo.premultipliedLast.rawValue

func read(_ path: String) -> Pixels? {
	guard
		let source = CGImageSourceCreateWithURL(URL(fileURLWithPath: path) as CFURL, nil),
		let image = CGImageSourceCreateImageAtIndex(source, 0, nil)
	else {
		return nil
	}

	var rgba = [UInt8](repeating: 0, count: image.width * image.height * 4)

	rgba.withUnsafeMutableBytes { buffer in
		let context = CGContext(data: buffer.baseAddress, width: image.width, height: image.height, bitsPerComponent: 8, bytesPerRow: image.width * 4, space: space, bitmapInfo: premultiplied)
		context?.draw(image, in: CGRect(x: 0, y: 0, width: image.width, height: image.height))
	}

	return (image.width, image.height, rgba)
}

func write(_ pixels: Pixels, to path: String) -> Bool {
	var rgba = pixels.rgba

	let image = rgba.withUnsafeMutableBytes { buffer in
		CGContext(data: buffer.baseAddress, width: pixels.width, height: pixels.height, bitsPerComponent: 8, bytesPerRow: pixels.width * 4, space: space, bitmapInfo: premultiplied)?.makeImage()
	}

	guard let image, let destination = CGImageDestinationCreateWithURL(URL(fileURLWithPath: path) as CFURL, "public.png" as CFString, 1, nil) else {
		return false
	}

	CGImageDestinationAddImage(destination, image, nil)

	return CGImageDestinationFinalize(destination)
}

let arguments = CommandLine.arguments

guard arguments.count == 3 || arguments.count == 4 else {
	FileHandle.standardError.write(Data("usage: image-diff <a.png> <b.png> [diff.png]\n".utf8))
	exit(2)
}

guard let a = read(arguments[1]), let b = read(arguments[2]) else {
	print("unreadable")
	exit(2)
}

if a.width != b.width || a.height != b.height {
	print("size \(a.width)x\(a.height) vs \(b.width)x\(b.height)")
	exit(1)
}

var diff = a
var differing = 0

for i in stride(from: 0, to: a.rgba.count, by: 4) {
	if (0 ..< 3).contains(where: { abs(Int(a.rgba[i + $0]) - Int(b.rgba[i + $0])) > 2 }) {
		differing += 1
		(diff.rgba[i], diff.rgba[i + 1], diff.rgba[i + 2], diff.rgba[i + 3]) = (255, 0, 0, 255)
	} else {
		// A quarter of each premultiplied channel: still under its alpha.
		for channel in i ..< i + 3 {
			diff.rgba[channel] /= 4
		}
	}
}

print(String(format: "%.4f%% (%d px of %dx%d)", Double(differing) * 100 / Double(a.width * a.height), differing, a.width, a.height))

if arguments.count == 4, !write(diff, to: arguments[3]) {
	FileHandle.standardError.write(Data("could not write \(arguments[3])\n".utf8))
	exit(2)
}
