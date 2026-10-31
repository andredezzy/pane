// Makes Pane's icons from the window renders in renders/ (Magnific, ADR 0089):
// one per sky, named for it.
// Usage: swift app/icon/make-icons.swift   (needs Xcode; run it under the
// Default icon style, System Settings › Appearance), then bun run lint, which
// formats the JSON it writes, then swift app/dmg/make-volume-icon.swift, whose
// volume icon is AppIconDay.
//
// Every icon has the one frame, Day's, whitened: each render gives only its
// glass. Day's frame keeps its shading and its colour neutral, and is close to
// white: a shade's distance from white, d, becomes d²/200, so the faces whiten
// most and the bevels keep their depth. Each sky's panes are cut from its own
// render, 20 pixels in from their outer sides (past the rounded corners' frame)
// and 3 from the mullion's, and stretched into Day's panes.
//
// - app/Pane/AppIcon.icon: the bundle's icon, Day by default and Night under
//   the dark icon style. Each window becomes a full-bleed layer, scaled so the
//   frame's outer edge sits 12 points past the canvas, so the system's squircle
//   mask cuts inside the frame and the icon conforms to its shape.
// - AppIcon<Sky> in Assets.xcassets, one per render: what the Dock shows while
//   Pane runs (applicationIconImage), and Settings' tiles. Each is macOS's own
//   drawing of an app whose icon is that window alone, so it matches the
//   bundle's icon to the pixel: the same 824-pixel body on a 1024 canvas, the
//   same shadow, the same tone.
// - Icon.iconset in Assets.xcassets: the document icon, Day's.
// - .github/assets/logo.png and logo-dark.png: the README's, Day's and Night's.
import AppKit
import ImageIO
import UniformTypeIdentifiers

let repo = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
let renders = repo.appendingPathComponent("app/icon/renders")
let pane = repo.appendingPathComponent("app/Pane")
let assets = pane.appendingPathComponent("Assets.xcassets")
// In the order Settings shows them: the sky through a day.
let variants = ["day", "clouds", "morning", "dusk", "fuchsia", "sunset", "twilight", "afterglow", "night", "moonrise"]
let fm = FileManager.default
let srgb = CGColorSpace(name: CGColorSpace.sRGB)!

// From the file's bytes, read now: an image source decodes lazily, and the
// scratch files go before their images are drawn.
func load(_ url: URL) -> CGImage {
	guard let data = try? Data(contentsOf: url), let source = CGImageSourceCreateWithData(data as CFData, nil),
	      let image = CGImageSourceCreateImageAtIndex(source, 0, [kCGImageSourceShouldCacheImmediately: true] as CFDictionary) else {
		fatalError("cannot read \(url.path)")
	}

	return image
}

func save(_ image: CGImage, _ url: URL) {
	try! fm.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
	let destination = CGImageDestinationCreateWithURL(url as CFURL, UTType.png.identifier as CFString, 1, nil)!
	CGImageDestinationAddImage(destination, image, nil)

	if !CGImageDestinationFinalize(destination) {
		fatalError("cannot write \(url.path)")
	}
}

func context(_ size: Int, opaque: Bool = false) -> CGContext {
	let info = opaque ? CGImageAlphaInfo.noneSkipLast : CGImageAlphaInfo.premultipliedLast
	return CGContext(data: nil, width: size, height: size, bitsPerComponent: 8, bytesPerRow: 0, space: srgb, bitmapInfo: info.rawValue)!
}

// An image's RGBA bytes, its top row first.
func pixels(_ image: CGImage) -> [UInt8] {
	var pixels = [UInt8](repeating: 0, count: image.width * image.height * 4)
	pixels.withUnsafeMutableBytes { buffer in
		CGContext(data: buffer.baseAddress, width: image.width, height: image.height, bitsPerComponent: 8, bytesPerRow: image.width * 4, space: srgb,
		          bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
			.draw(image, in: CGRect(x: 0, y: 0, width: image.width, height: image.height))
	}
	return pixels
}

// The window frame's box in a render: where each middle row and column first
// leaves the backdrop (sampled at the image's edge), the median of five. The
// soft shadow under the window takes a larger step to leave.
func frameBox(_ image: CGImage) -> CGRect {
	let w = image.width, h = image.height
	let pixels = pixels(image)

	// Row 0 is the image's top row.
	func color(_ x: Int, _ y: Int) -> [Int] {
		let i = (y * w + x) * 4
		return [Int(pixels[i]), Int(pixels[i + 1]), Int(pixels[i + 2])]
	}

	func mean(_ points: [(Int, Int)]) -> [Int] {
		var sum = [0, 0, 0]
		for point in points { for (k, value) in color(point.0, point.1).enumerated() { sum[k] += value } }
		return sum.map { $0 / points.count }
	}

	func differs(_ point: (Int, Int), from reference: [Int], by threshold: Int) -> Bool {
		zip(color(point.0, point.1), reference).contains { abs($0 - $1) > threshold }
	}

	func edge(_ path: [(Int, Int)], from reference: [Int], by threshold: Int) -> Int {
		guard let index = path.indices.dropLast().first(where: { differs(path[$0], from: reference, by: threshold) && differs(path[$0 + 1], from: reference, by: threshold) }) else {
			fatalError("no window frame found")
		}

		return index
	}

	func median(_ values: [Int]) -> Int { values.sorted()[values.count / 2] }
	let probes = [-120, -60, 0, 60, 120]
	let left = median(probes.map { d in let y = h / 2 + d; return 48 + edge((48 ..< w / 2).map { ($0, y) }, from: mean((8 ..< 48).map { ($0, y) }), by: 10) })
	let right = median(probes.map { d in let y = h / 2 + d; return w - 49 - edge(stride(from: w - 49, to: w / 2, by: -1).map { ($0, y) }, from: mean((w - 48 ..< w - 8).map { ($0, y) }), by: 10) })
	let top = median(probes.map { d in let x = w / 2 + d; return 48 + edge((48 ..< h / 2).map { (x, $0) }, from: mean((8 ..< 48).map { (x, $0) }), by: 10) })
	let bottom = median(probes.map { d in let x = w / 2 + d; return h - 49 - edge(stride(from: h - 49, to: h / 2, by: -1).map { (x, $0) }, from: mean((h - 48 ..< h - 8).map { (x, $0) }), by: 30) })
	return CGRect(x: left, y: top, width: right - left + 1, height: bottom - top + 1)
}

// A full-bleed 1024 layer: the frame's box, made square about its centre,
// spans the canvas plus 12 points on each side.
func layer(_ render: CGImage, box: CGRect) -> CGImage {
	let scale = (1024 + 2 * 12) / max(box.width, box.height)
	let canvas = context(1024, opaque: true)
	canvas.interpolationQuality = .high
	let width = CGFloat(render.width) * scale, height = CGFloat(render.height) * scale
	let x = 512 - box.midX * scale, top = 512 - box.midY * scale
	canvas.draw(render, in: CGRect(x: x, y: 1024 - top - height, width: width, height: height))
	return canvas.makeImage()!
}

// A render's glass box: where luminance drops hardest entering the glass, past
// the frame's dark seal, the median along five rows or columns. The renders
// share their reference's layout, so each edge is looked for near Day's.
func glassBox(_ pixels: [UInt8], width w: Int) -> (left: Int, right: Int, top: Int, bottom: Int) {
	func lum(_ x: Int, _ y: Int) -> Int {
		let i = (y * w + x) * 4
		return (Int(pixels[i]) * 299 + Int(pixels[i + 1]) * 587 + Int(pixels[i + 2]) * 114) / 1000
	}

	func drop(_ path: [(Int, Int)]) -> Int {
		(1 ..< path.count).max { lum(path[$0 - 1].0, path[$0 - 1].1) - lum(path[$0].0, path[$0].1) < lum(path[$1 - 1].0, path[$1 - 1].1) - lum(path[$1].0, path[$1].1) }!
	}

	func median(_ values: [Int]) -> Int { values.sorted()[values.count / 2] }
	let rows = [380, 450, 512, 580, 650], columns = [300, 360, 420, 620, 690]
	return (
		median(rows.map { y in 180 + drop((180 ... 260).map { ($0, y) }) }),
		median(rows.map { y in 840 - drop(stride(from: 840, through: 760, by: -1).map { ($0, y) }) }),
		median(columns.map { x in 180 + drop((180 ... 260).map { (x, $0) }) }),
		median(columns.map { x in 850 - drop(stride(from: 850, through: 760, by: -1).map { (x, $0) }) })
	)
}

// Day's render, whose frame every icon has.
let master = load(renders.appendingPathComponent("day.png"))
let masterPixels = pixels(master)
let mw = master.width, mh = master.height
let masterBox = frameBox(master)
let masterGlass = glassBox(masterPixels, width: mw)
// Day's mullion, where luminance jumps leaving the glass each side of the middle.
let mullion: (left: Int, right: Int) = {
	func lum(_ x: Int) -> Int {
		let i = (mh / 2 * mw + x) * 4
		return (Int(masterPixels[i]) * 299 + Int(masterPixels[i + 1]) * 587 + Int(masterPixels[i + 2]) * 114) / 1000
	}

	let left = (440 ... 512).max { lum($0) - lum($0 - 1) < lum($1) - lum($1 - 1) }!
	let right = (512 ... 584).max { lum($0 - 1) - lum($0) < lum($1 - 1) - lum($1) }!
	return (left, right)
}()

// A render's two panes (top-down pixel rects), split at Day's mullion.
func panes(_ box: (left: Int, right: Int, top: Int, bottom: Int)) -> [CGRect] {
	[CGRect(x: box.left, y: box.top, width: mullion.left - box.left, height: box.bottom + 1 - box.top),
	 CGRect(x: mullion.right, y: box.top, width: box.right + 1 - mullion.right, height: box.bottom + 1 - box.top)]
}

let masterPanes = panes(masterGlass)

// Day's glass, softly: inside a pane, bluer than the frame (B - R over 12) and
// lighter than the dark seal (luminance over 75), with the holes a fill from
// the pane's edge cannot reach; then a 5x5 majority evens the edge and a 3x3
// mean softens it.
let glassAlpha: [Double] = {
	var glass = [Bool](repeating: false, count: mw * mh)

	func isGlass(_ x: Int, _ y: Int) -> Bool {
		let i = (y * mw + x) * 4
		let r = Int(masterPixels[i]), g = Int(masterPixels[i + 1]), b = Int(masterPixels[i + 2])
		return b - r > 12 && (r * 299 + g * 587 + b * 114) / 1000 > 75
	}

	for pane in masterPanes {
		let x0 = Int(pane.minX), x1 = Int(pane.maxX), y0 = Int(pane.minY), y1 = Int(pane.maxY)
		var open = [Bool](repeating: false, count: mw * mh)
		var queue: [(Int, Int)] = []

		for x in x0 ..< x1 { for y in [y0, y1 - 1] where !isGlass(x, y) { open[y * mw + x] = true; queue.append((x, y)) } }
		for y in y0 ..< y1 { for x in [x0, x1 - 1] where !isGlass(x, y) { open[y * mw + x] = true; queue.append((x, y)) } }

		var head = 0
		while head < queue.count {
			let (x, y) = queue[head]
			head += 1

			for (dx, dy) in [(1, 0), (-1, 0), (0, 1), (0, -1)] {
				let nx = x + dx, ny = y + dy
				guard nx >= x0, nx < x1, ny >= y0, ny < y1, !open[ny * mw + nx], !isGlass(nx, ny) else { continue }
				open[ny * mw + nx] = true
				queue.append((nx, ny))
			}
		}

		for y in y0 ..< y1 { for x in x0 ..< x1 where !open[y * mw + x] { glass[y * mw + x] = true } }
	}

	var even = glass
	for y in 2 ..< mh - 2 { for x in 2 ..< mw - 2 {
		var n = 0
		for dy in -2 ... 2 { for dx in -2 ... 2 where glass[(y + dy) * mw + x + dx] { n += 1 } }
		even[y * mw + x] = n >= 13
	} }

	var alpha = [Double](repeating: 0, count: mw * mh)
	for y in 1 ..< mh - 1 { for x in 1 ..< mw - 1 {
		var n = 0
		for dy in -1 ... 1 { for dx in -1 ... 1 where even[(y + dy) * mw + x + dx] { n += 1 } }
		alpha[y * mw + x] = Double(n) / 9
	} }

	return alpha
}()

// The frame whitened: a shade's distance from white, d, becomes d²/200, eased
// in above the seal's darkness so the seal keeps its line. The broad faces, 47
// levels from white, come to 11, while the deeper shade of the bevels and of
// the lip by the glass (76 levels, to 29) keeps its depth.
func whitened(_ l: Double) -> Double {
	let t = min(max((l - 90) / 70, 0), 1), ease = t * t * (3 - 2 * t)
	return l + (255 - (255 - l) * (255 - l) / 200 - l) * ease
}

// Day's whitened frame around a sky's panes.
func composite(_ sky: CGImage) -> CGImage {
	let skyPanes = panes(glassBox(pixels(sky), width: sky.width))
	let glassLayer = CGContext(data: nil, width: mw, height: mh, bitsPerComponent: 8, bytesPerRow: mw * 4, space: srgb,
	                           bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
	glassLayer.interpolationQuality = .high

	for (index, (source, destination)) in zip(skyPanes, masterPanes).enumerated() {
		let left = index == 0 ? 20.0 : 3.0, right = index == 0 ? 3.0 : 20.0
		let cut = CGRect(x: source.minX + left, y: source.minY + 20, width: source.width - left - right, height: source.height - 40)
		let into = destination.insetBy(dx: -2, dy: -2)
		// CoreGraphics draws bottom-up.
		glassLayer.draw(sky.cropping(to: cut)!, in: CGRect(x: into.minX, y: CGFloat(mh) - into.maxY, width: into.width, height: into.height))
	}

	let glass = glassLayer.data!.assumingMemoryBound(to: UInt8.self)
	var out = [UInt8](repeating: 255, count: mw * mh * 4)

	for i in 0 ..< mw * mh {
		let p = i * 4, k = glassAlpha[i]
		let frame = whitened(0.299 * Double(masterPixels[p]) + 0.587 * Double(masterPixels[p + 1]) + 0.114 * Double(masterPixels[p + 2]))

		for c in 0 ..< 3 {
			out[p + c] = UInt8(max(0, min(255, (Double(glass[p + c]) * k + frame * (1 - k)).rounded())))
		}
	}

	return out.withUnsafeMutableBytes { buffer in
		CGContext(data: buffer.baseAddress, width: mw, height: mh, bitsPerComponent: 8, bytesPerRow: mw * 4, space: srgb,
		          bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!.makeImage()!
	}
}

// An Icon Composer document of one layer, with an image per appearance.
func writeIcon(_ url: URL, images: [(appearance: String?, name: String, image: CGImage)]) {
	try? fm.removeItem(at: url)

	for entry in images {
		save(entry.image, url.appendingPathComponent("Assets/\(entry.name)"))
	}

	let specializations = images.map { entry in
		entry.appearance.map { ["appearance": $0, "value": entry.name] } ?? ["value": entry.name]
	}
	let document: [String: Any] = [
		"groups": [[
			"layers": [["glass": false, "image-name-specializations": specializations, "name": "Window"]],
			"shadow": ["kind": "neutral", "opacity": 0.5],
			"specular": false,
			"translucency": ["enabled": false, "value": 0.5],
		]],
		"supported-platforms": ["squares": ["macOS"]],
	]
	let json = try! JSONSerialization.data(withJSONObject: document, options: [.prettyPrinted, .sortedKeys])
	try! json.write(to: url.appendingPathComponent("icon.json"))
}

func run(_ arguments: [String]) {
	let process = Process()
	process.executableURL = URL(fileURLWithPath: "/usr/bin/xcrun")
	process.arguments = arguments
	process.standardOutput = FileHandle.nullDevice
	try! process.run()
	process.waitUntilExit()

	if process.terminationStatus != 0 {
		fatalError("xcrun \(arguments.joined(separator: " ")) failed")
	}
}

// How macOS draws an app whose icon holds `images`, at 1024 pixels: a scratch
// bundle with actool's build of that icon, which NSWorkspace draws as Finder
// and the Dock do. It needs an executable to count as an app.
func systemIcon(_ images: [(appearance: String?, name: String, image: CGImage)]) -> CGImage {
	let scratch = fm.temporaryDirectory.appendingPathComponent("pane-icon-\(UUID().uuidString)")
	defer { try? fm.removeItem(at: scratch) }
	let app = scratch.appendingPathComponent("Icon.app/Contents")
	let resources = app.appendingPathComponent("Resources")
	try! fm.createDirectory(at: resources, withIntermediateDirectories: true)
	try! fm.createDirectory(at: app.appendingPathComponent("MacOS"), withIntermediateDirectories: true)

	let document = scratch.appendingPathComponent("AppIcon.icon")
	writeIcon(document, images: images)
	run(["actool", "--compile", resources.path, "--platform", "macosx", "--minimum-deployment-target", "14.0", "--app-icon", "AppIcon",
	     "--output-partial-info-plist", scratch.appendingPathComponent("partial.plist").path, document.path])

	try! Data("#!/bin/sh\n".utf8).write(to: app.appendingPathComponent("MacOS/Icon"))
	try! fm.setAttributes([.posixPermissions: 0o755], ofItemAtPath: app.appendingPathComponent("MacOS/Icon").path)
	let info: [String: Any] = [
		"CFBundleExecutable": "Icon", "CFBundleIconFile": "AppIcon", "CFBundleIconName": "AppIcon",
		"CFBundleIdentifier": "xyz.roundedcorners.pane.icon.\(UUID().uuidString)", "CFBundlePackageType": "APPL",
	]
	try! PropertyListSerialization.data(fromPropertyList: info, format: .xml, options: 0).write(to: app.appendingPathComponent("Info.plist"))

	var rect = NSRect(x: 0, y: 0, width: 1024, height: 1024)
	let drawn = NSWorkspace.shared.icon(forFile: scratch.appendingPathComponent("Icon.app").path).cgImage(forProposedRect: &rect, context: nil, hints: nil)!
	let canvas = context(1024)
	canvas.draw(drawn, in: CGRect(x: 0, y: 0, width: 1024, height: 1024))
	return canvas.makeImage()!
}

func resized(_ image: CGImage, _ size: Int) -> CGImage {
	let canvas = context(size)
	canvas.interpolationQuality = .high
	canvas.draw(image, in: CGRect(x: 0, y: 0, width: size, height: size))
	return canvas.makeImage()!
}

let layers = Dictionary(uniqueKeysWithValues: variants.map { ($0, layer(composite(load(renders.appendingPathComponent("\($0).png"))), box: masterBox)) })

writeIcon(pane.appendingPathComponent("AppIcon.icon"), images: [(nil, "day.png", layers["day"]!), ("dark", "night.png", layers["night"]!)])

for variant in variants {
	let icon = systemIcon([(nil, "\(variant).png", layers[variant]!)])

	let name = "AppIcon\(variant.capitalized)"
	let set = assets.appendingPathComponent("\(name).imageset")
	try? fm.removeItem(at: set)
	save(icon, set.appendingPathComponent("\(name).png"))
	// At 2x, so the image is 512 points, as an app icon is.
	let contents = #"{"images":[{"filename":"\#(name).png","idiom":"universal","scale":"2x"}],"info":{"author":"xcode","version":1}}"#
	try! Data(contents.utf8).write(to: set.appendingPathComponent("Contents.json"))

	if variant == "day" {
		save(resized(icon, 256), assets.appendingPathComponent("Icon.iconset/icon_256x256.png"))
		save(resized(icon, 512), assets.appendingPathComponent("Icon.iconset/icon_256x256@2x.png"))
		save(resized(icon, 512), repo.appendingPathComponent(".github/assets/logo.png"))
	} else if variant == "night" {
		save(resized(icon, 512), repo.appendingPathComponent(".github/assets/logo-dark.png"))
	}

	print("\(name): ok")
}
