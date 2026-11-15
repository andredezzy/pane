// Makes Pane's icons from the window renders in renders/ (Magnific, ADR 0089):
// one per sky, named for it.
// Usage: swift packaging/icon/make-icons.swift   (needs Xcode; run it under the
// Default icon style, System Settings › Appearance), then bun run lint, which
// formats the JSON it writes, then swift packaging/dmg/make-volume-icon.swift, whose
// volume icon is AppIconDay.
//        swift packaging/icon/make-icons.swift windows   (Windows' icons alone,
// from the AppIcon PNGs as they are: no Xcode, and the Mac's icons untouched)
//
// Every icon has the one frame, Day's, whitened: each render gives only its
// glass. Day's frame keeps its shading and its colour neutral, and is close to
// white: a shade's distance from white, d, becomes d²/200, so the faces whiten
// most and the bevels keep their depth. Each sky's panes are cut from its own
// render, 20 pixels in from their outer sides (past the rounded corners' frame)
// and 3 from the mullion's, and stretched into Day's panes.
//
// - packaging/icon/AppIcon.icon: the bundle's icon, Day by default and Night under
//   the dark icon style (package.sh compiles it). Each window becomes a full-bleed layer, scaled so the
//   frame's outer edge sits 12 points past the canvas, so the system's squircle
//   mask cuts inside the frame and the icon conforms to its shape.
// - pane/ui/panels/app_icons/AppIcon<Sky>.png, one per render: what the Dock
//   shows while Pane runs (applicationIconImage), and Settings' tiles, which
//   the framework carries in its Resources (pane/BUILD.gn). Each is macOS's
//   own drawing of an app whose icon is that window alone, so it matches the
//   bundle's icon to the pixel: the same 824-pixel body on a 1024 canvas, the
//   same shadow, the same tone, 512 points at 2×.
// - .github/assets/logo.png and logo-dark.png: the README's, Day's and Night's.
// - Windows' icons, beside the PNGs, each drawn down from them (Paper 11.12):
//   Pane.ico, the app's (Day), which chrome.exe carries and the shortcuts,
//   Installed apps, and Default apps name; AppIcon-<Sky>.ico, one per sky, for
//   the shortcuts that follow the App icon setting; and PaneHTM.ico and
//   PanePDF.ico, the documents' (makeWindowsIcons, at the end).
import AppKit
import ImageIO
import UniformTypeIdentifiers

let repo = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
let renders = repo.appendingPathComponent("packaging/icon/renders")
let bundleIcon = repo.appendingPathComponent("packaging/icon/AppIcon.icon")
let skies = repo.appendingPathComponent("pane/ui/panels/app_icons")
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

// Windows' icons alone: the macOS drawing below needs Xcode and the Default
// icon style, and would make the Mac's PNGs again.
if CommandLine.arguments.dropFirst().first == "windows" {
	makeWindowsIcons()
	exit(0)
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

writeIcon(bundleIcon, images: [(nil, "day.png", layers["day"]!), ("dark", "night.png", layers["night"]!)])

for variant in variants {
	let icon = systemIcon([(nil, "\(variant).png", layers[variant]!)])

	save(icon, skies.appendingPathComponent("AppIcon\(variant.capitalized).png"))

	if variant == "day" {
		save(resized(icon, 512), repo.appendingPathComponent(".github/assets/logo.png"))
	} else if variant == "night" {
		save(resized(icon, 512), repo.appendingPathComponent(".github/assets/logo-dark.png"))
	}

	print("\(variant): ok")
}

makeWindowsIcons()

// MARK: Windows' icons (Paper 11.12.1, 11.12.16's D, 11.12.2's F)

// Skia's Lanczos3 (skia::ImageOperations::RESIZE_BEST), with which Views
// draws the window's own icon down to each size Windows asks for (IconUtil),
// so a file's icon is the image the window shows: each output pixel weighs the
// source within 3 of its own pixels, the lobes widened by the scale, the
// weights summing to one; premultiplied channels clamped, and the alpha kept
// at or over each colour.
func lanczos3(_ x: Double) -> Double {
	if x <= -3 || x >= 3 {
		return 0
	}

	if abs(x) < 1e-7 {
		return 1
	}

	let xpi = x * Double.pi
	return sin(xpi) / xpi * sin(xpi / 3) / (xpi / 3)
}

// For each output pixel along a side, the first source pixel it reads and
// the weights from there.
func lanczosFilters(_ source: Int, _ size: Int) -> [(first: Int, weights: [Double])] {
	let scale = Double(size) / Double(source), clamped = min(1, scale)
	let support = 3 / clamped

	return (0 ..< size).map { index in
		let centre = (Double(index) + 0.5) / scale
		let first = max(0, Int((centre - support).rounded(.down)))
		let last = min(source - 1, Int((centre + support).rounded(.up)))
		let weights = (first ... last).map { lanczos3((Double($0) + 0.5 - centre) * clamped) }
		let sum = weights.reduce(0, +)
		return (first, weights.map { $0 / sum })
	}
}

// `image` drawn down to `size` by side, from its premultiplied pixels when
// they are at hand.
func drawnDown(_ image: CGImage, to size: Int, pixels known: [UInt8]? = nil) -> CGImage {
	let source = known ?? pixels(image), w = image.width, h = image.height
	let across = lanczosFilters(w, size), down = lanczosFilters(h, size)
	var rows = [Double](repeating: 0, count: size * h * 4)

	source.withUnsafeBufferPointer { source in
		rows.withUnsafeMutableBufferPointer { rows in
			for y in 0 ..< h {
				for (x, filter) in across.enumerated() {
					var sum = (0.0, 0.0, 0.0, 0.0)
					for (k, weight) in filter.weights.enumerated() {
						let i = (y * w + filter.first + k) * 4
						sum.0 += weight * Double(source[i])
						sum.1 += weight * Double(source[i + 1])
						sum.2 += weight * Double(source[i + 2])
						sum.3 += weight * Double(source[i + 3])
					}
					let o = (y * size + x) * 4
					rows[o] = min(255, max(0, sum.0))
					rows[o + 1] = min(255, max(0, sum.1))
					rows[o + 2] = min(255, max(0, sum.2))
					rows[o + 3] = min(255, max(0, sum.3))
				}
			}
		}
	}

	var out = [UInt8](repeating: 0, count: size * size * 4)
	for x in 0 ..< size {
		for (y, filter) in down.enumerated() {
			var sum = (0.0, 0.0, 0.0, 0.0)
			for (k, weight) in filter.weights.enumerated() {
				let i = ((filter.first + k) * size + x) * 4
				sum.0 += weight * rows[i]
				sum.1 += weight * rows[i + 1]
				sum.2 += weight * rows[i + 2]
				sum.3 += weight * rows[i + 3]
			}
			let byte = { (value: Double) in UInt8(min(255, max(0, value.rounded()))) }
			let o = (y * size + x) * 4
			out[o] = byte(sum.0)
			out[o + 1] = byte(sum.1)
			out[o + 2] = byte(sum.2)
			out[o + 3] = max(byte(sum.3), out[o], out[o + 1], out[o + 2])
		}
	}

	return out.withUnsafeMutableBytes { buffer in
		CGContext(data: buffer.baseAddress, width: size, height: size, bitsPerComponent: 8, bytesPerRow: size * 4, space: srgb,
		          bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!.makeImage()!
	}
}

func littleEndian<T: FixedWidthInteger>(_ value: T) -> Data {
	withUnsafeBytes(of: value.littleEndian) { Data($0) }
}

// One size's image in a .ico: below 256 a 32-bit bitmap, its rows bottom-up,
// its colours unpremultiplied, then the AND mask, set where the image is
// wholly clear; the 256 a PNG, as Windows' own icons and Chromium's IconUtil
// keep it.
func icoImage(_ image: CGImage) -> Data {
	if image.width == 256 {
		let png = NSMutableData()
		let destination = CGImageDestinationCreateWithData(png, UTType.png.identifier as CFString, 1, nil)!
		CGImageDestinationAddImage(destination, image, nil)

		if !CGImageDestinationFinalize(destination) {
			fatalError("cannot make the 256's PNG")
		}

		return png as Data
	}

	let size = image.width, rgba = pixels(image)
	let maskRow = ((size + 31) / 32) * 4
	var data = Data()
	data += littleEndian(UInt32(40))
	data += littleEndian(Int32(size))
	// The height counts the colours and the mask.
	data += littleEndian(Int32(size * 2))
	data += littleEndian(UInt16(1))
	data += littleEndian(UInt16(32))
	data += littleEndian(UInt32(0))
	data += littleEndian(UInt32(size * size * 4 + maskRow * size))
	data += Data(count: 16)

	for y in (0 ..< size).reversed() {
		for x in 0 ..< size {
			let i = (y * size + x) * 4, alpha = rgba[i + 3]
			let straight = { (c: UInt8) in alpha == 0 ? UInt8(0) : UInt8(min(255, (Double(c) * 255 / Double(alpha)).rounded())) }
			data += [straight(rgba[i + 2]), straight(rgba[i + 1]), straight(rgba[i]), alpha]
		}
	}

	for y in (0 ..< size).reversed() {
		var row = [UInt8](repeating: 0, count: maskRow)
		for x in 0 ..< size where rgba[(y * size + x) * 4 + 3] == 0 {
			row[x / 8] |= 0x80 >> UInt8(x % 8)
		}
		data += row
	}

	return data
}

// A .ico of one image per size, smallest first: its directory, then each
// image (Microsoft's ICONDIR and ICONDIRENTRY, Chromium's IconUtil's layout).
func writeIco(_ images: [CGImage], _ url: URL) {
	let parts = images.map(icoImage)
	var data = Data()
	data += littleEndian(UInt16(0))
	data += littleEndian(UInt16(1))
	data += littleEndian(UInt16(images.count))
	var offset = 6 + 16 * images.count

	for (image, part) in zip(images, parts) {
		// 256 is written 0.
		data += [UInt8(image.width % 256), UInt8(image.height % 256), 0, 0]
		data += littleEndian(UInt16(1))
		data += littleEndian(UInt16(32))
		data += littleEndian(UInt32(part.count))
		data += littleEndian(UInt32(offset))
		offset += part.count
	}

	for part in parts {
		data += part
	}

	try! data.write(to: url)
}

func color(_ hex: UInt32, _ alpha: CGFloat = 1) -> CGColor {
	CGColor(srgbRed: CGFloat(hex >> 16 & 0xFF) / 255, green: CGFloat(hex >> 8 & 0xFF) / 255, blue: CGFloat(hex & 0xFF) / 255, alpha: alpha)
}

// A document's icon at `size` (Paper 11.12.2, F, placement 1, the user's
// call): a portrait page with its fold, the window centred under the fold,
// and on the PDF's the window raised over a "PDF" tag at the page's foot, a
// bar with no letters below 48. The board's geometry, as fractions of the
// side: the page 0.15 to 0.85 across and 0.03 to 0.97 down, its fold 0.22,
// edged in #B8BEC6 (1 pixel, 2 at 256), white to #EEF0F3, with #DDE1E6 under
// the fold; from 32 up its shadow, the page again 1 pixel lower (0.012 of the
// side past 64) at 10% black; the window 0.58 of the side, its middle 0.59
// down, or the PDF's 0.52 at 0.49 over the tag, 0.27 and 0.76 to its corner,
// 0.46 by 0.14, #2B2D31, corners 0.03 of the side (0.5 pixel below 32), its
// letters white, bold, 0.62 of its height, 0.03 em apart. The window and the
// tag fall on whole pixels as the board places them, and from 16 to 48 the
// page's edge and fold do too, a shadow keeping the bottom row. The letters
// are the board's Inter Bold (fonts/Inter-Bold.ttf, under the SIL Open Font
// License beside it), which no Mac carries, so it is registered for this run.
func documentIcon(pdf: Bool, size: Int, window: CGImage, windowPixels: [UInt8]) -> CGImage {
	let side = CGFloat(size)
	let shadow: CGFloat? = size >= 32 ? max(1, 0.012 * side) : nil
	let line: CGFloat = size <= 96 ? 1 : (side / 128).rounded()
	var left = 0.15 * side, right = 0.85 * side, top = 0.03 * side, bottom = 0.97 * side
	var fold = 0.22 * side

	// The edge's middle half a pixel in from a whole one, so its pixel is
	// whole: the side's fractions outward to the nearest such line, the fold
	// whole.
	if size <= 48 {
		left = left.rounded(.down) + 0.5
		right = right.rounded(.up) - 0.5
		top = top.rounded(.down) + 0.5
		bottom = bottom.rounded(.up) - 0.5
		fold = fold.rounded()

		if shadow != nil {
			bottom = min(bottom, side - 1.5)
		}
	}

	let canvas = context(size)
	canvas.translateBy(x: 0, y: side)
	canvas.scaleBy(x: 1, y: -1)
	canvas.interpolationQuality = .none

	func page(lower: CGFloat = 0) -> CGPath {
		let path = CGMutablePath()
		path.move(to: CGPoint(x: left, y: top + lower))
		path.addLine(to: CGPoint(x: right - fold, y: top + lower))
		path.addLine(to: CGPoint(x: right, y: top + fold + lower))
		path.addLine(to: CGPoint(x: right, y: bottom + lower))
		path.addLine(to: CGPoint(x: left, y: bottom + lower))
		path.closeSubpath()
		return path
	}

	if let shadow {
		canvas.addPath(page(lower: shadow))
		canvas.setFillColor(color(0x000000, 0.1))
		canvas.fillPath()
	}

	canvas.saveGState()
	canvas.addPath(page())
	canvas.clip()
	let gradient = CGGradient(colorsSpace: srgb, colors: [color(0xFFFFFF), color(0xEEF0F3)] as CFArray, locations: [0, 1])!
	canvas.drawLinearGradient(gradient, start: CGPoint(x: 0, y: top), end: CGPoint(x: 0, y: bottom), options: [])
	canvas.restoreGState()

	canvas.setStrokeColor(color(0xB8BEC6))
	canvas.setLineWidth(line)
	canvas.setLineJoin(.round)
	canvas.addPath(page())
	canvas.strokePath()

	let flap = CGMutablePath()
	flap.move(to: CGPoint(x: right - fold, y: top))
	flap.addLine(to: CGPoint(x: right - fold, y: top + fold))
	flap.addLine(to: CGPoint(x: right, y: top + fold))
	flap.closeSubpath()
	canvas.addPath(flap)
	canvas.setFillColor(color(0xDDE1E6))
	canvas.fillPath()
	canvas.addPath(flap)
	canvas.strokePath()

	// The window, its own pixels at whole ones (the board rounds half to even).
	let glyph = ((pdf ? 0.52 : 0.58) * side).rounded(.toNearestOrEven)
	let x = (side / 2 - glyph / 2).rounded(.toNearestOrEven)
	let y = ((pdf ? 0.49 : 0.59) * side - glyph / 2).rounded(.toNearestOrEven)
	canvas.saveGState()
	canvas.translateBy(x: x, y: y + glyph)
	canvas.scaleBy(x: 1, y: -1)
	canvas.draw(drawnDown(window, to: Int(glyph), pixels: windowPixels), in: CGRect(x: 0, y: 0, width: glyph, height: glyph))
	canvas.restoreGState()

	if pdf {
		var tag = CGRect(x: 0.27 * side, y: 0.76 * side, width: 0.46 * side, height: 0.14 * side)

		if size <= 48 {
			tag = CGRect(x: tag.minX.rounded(.toNearestOrEven), y: tag.minY.rounded(.toNearestOrEven),
			             width: max(2, tag.width.rounded(.toNearestOrEven)), height: max(2, tag.height.rounded(.toNearestOrEven)))
		}

		let corner = size >= 32 ? max(1, 0.03 * side) : 0.5
		canvas.addPath(CGPath(roundedRect: tag, cornerWidth: corner, cornerHeight: corner, transform: nil))
		canvas.setFillColor(color(0x2B2D31))
		canvas.fillPath()

		if size >= 48 {
			// The board's line box: as tall as the font, its middle the tag's,
			// the baseline where the font's ascent and descent put it.
			let font = NSFont(name: "Inter-Bold", size: 0.62 * tag.height)!
			let text = NSAttributedString(string: "PDF", attributes: [
				.font: font, .foregroundColor: NSColor.white, .kern: 0.03 * font.pointSize,
			])
			let line = CTLineCreateWithAttributedString(text)
			let width = CGFloat(CTLineGetTypographicBounds(line, nil, nil, nil))
			let leading = (font.pointSize - (font.ascender - font.descender)) / 2
			let baseline = tag.midY - font.pointSize / 2 + leading + font.ascender
			canvas.saveGState()
			canvas.translateBy(x: tag.midX - width / 2, y: baseline)
			canvas.scaleBy(x: 1, y: -1)
			canvas.textPosition = .zero
			CTLineDraw(line, canvas)
			canvas.restoreGState()
		}
	}

	return canvas.makeImage()!
}

// The sizes each .ico holds: Windows takes the size a place asks for, 16 to
// 32 in lists and on the taskbar, 48 to 256 on the desktop and in Explorer,
// and draws the next larger one down where it is missing (Paper 11.12.1).
func makeWindowsIcons() {
	// The PDF tag's letters (documentIcon).
	let inter = repo.appendingPathComponent("packaging/icon/fonts/Inter-Bold.ttf")
	guard CTFontManagerRegisterFontsForURL(inter as CFURL, .process, nil) else {
		fatalError("could not register \(inter.path)")
	}
	let sizes = [16, 20, 24, 32, 40, 48, 64, 96, 256]

	for variant in variants {
		let window = load(skies.appendingPathComponent("AppIcon\(variant.capitalized).png"))
		let windowPixels = pixels(window)
		let images = sizes.map { drawnDown(window, to: $0, pixels: windowPixels) }
		writeIco(images, skies.appendingPathComponent("AppIcon-\(variant.capitalized).ico"))

		// The app's own is Day, the sky the Mac's bundle carries.
		if variant == "day" {
			writeIco(images, skies.appendingPathComponent("Pane.ico"))
			for (pdf, name) in [(false, "PaneHTM.ico"), (true, "PanePDF.ico")] {
				let documents = sizes.map { documentIcon(pdf: pdf, size: $0, window: window, windowPixels: windowPixels) }
				writeIco(documents, skies.appendingPathComponent(name))
			}
		}

		print("\(variant).ico: ok")
	}
}
