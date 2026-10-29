// Makes the dmg's volume icon, VolumeIcon.icns beside this file: Pane's icon,
// Day, as macOS draws it (app/icon/make-icons.swift's AppIconDay), with its
// 16-point art 1.5 points lower. Finder's title bar sets the volume's icon
// there beside its title, "Pane", with the icon's centre 1.5 points above the
// title's, from its capitals' tops to its baseline; lowered, the two share a
// centre. The other sizes, for the desktop and Get Info, stand as drawn.
// Usage: swift app/dmg/make-volume-icon.swift
import AppKit
import ImageIO
import UniformTypeIdentifiers

let directory = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
let source = directory.appendingPathComponent("../Pane/Assets.xcassets/AppIconDay.imageset/AppIconDay.png").standardizedFileURL
let srgb = CGColorSpace(name: CGColorSpace.sRGB)!

// How far the 16-point art drops, in points.
let lowered = 1.5

guard let data = try? Data(contentsOf: source), let decoded = CGImageSourceCreateWithData(data as CFData, nil),
      let icon = CGImageSourceCreateImageAtIndex(decoded, 0, [kCGImageSourceShouldCacheImmediately: true] as CFDictionary) else {
	fatalError("cannot read \(source.path)")
}

// The icon at `points`, at `scale`, `drop` points lower.
func drawn(points: Int, scale: Int, drop: Double) -> CGImage {
	let size = points * scale
	let canvas = CGContext(data: nil, width: size, height: size, bitsPerComponent: 8, bytesPerRow: 0, space: srgb, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
	canvas.interpolationQuality = .high
	canvas.draw(icon, in: CGRect(x: 0, y: -drop * Double(scale), width: Double(size), height: Double(size)))
	return canvas.makeImage()!
}

let fm = FileManager.default
let iconset = fm.temporaryDirectory.appendingPathComponent("pane-volume-\(UUID().uuidString).iconset")
try! fm.createDirectory(at: iconset, withIntermediateDirectories: true)
defer { try? fm.removeItem(at: iconset) }

for points in [16, 32, 128, 256] {
	for scale in [1, 2] {
		let name = scale == 1 ? "icon_\(points)x\(points).png" : "icon_\(points)x\(points)@2x.png"
		let destination = CGImageDestinationCreateWithURL(iconset.appendingPathComponent(name) as CFURL, UTType.png.identifier as CFString, 1, nil)!
		CGImageDestinationAddImage(destination, drawn(points: points, scale: scale, drop: points == 16 ? lowered : 0), nil)

		if !CGImageDestinationFinalize(destination) {
			fatalError("cannot write \(name)")
		}
	}
}

let iconutil = Process()
iconutil.executableURL = URL(fileURLWithPath: "/usr/bin/iconutil")
iconutil.arguments = ["--convert", "icns", "--output", directory.appendingPathComponent("VolumeIcon.icns").path, iconset.path]
try! iconutil.run()
iconutil.waitUntilExit()

if iconutil.terminationStatus != 0 {
	fatalError("iconutil failed")
}

print("VolumeIcon.icns: ok")
