// Draws the video, one 3840×2160 RGBA frame at a time: the aerial's raw frames
// in on stdin, the finished frames out on stdout. Over the aerial (dimmed to
// 0.8) sits a Liquid Glass dock with Finder, Pane, and Paper, Pane's icon
// showing the sky frames.txt names for the frame; above, "Pane" in its
// wordmark type and "You choose".
// Usage: compositor <repo> <build dir>   (render.sh builds it with -O and pipes it)
import Accelerate
import AppKit
import CoreText
import Foundation
import ImageIO

let repo = CommandLine.arguments[1], build = CommandLine.arguments[2]
let W = 3840, H = 2160, frameBytes = W * H * 4
let skies = ["Day", "Clouds", "Morning", "Dusk", "Fuchsia", "Sunset", "Twilight", "Afterglow", "Night", "Moonrise"]
let order = try! String(contentsOfFile: "\(build)/frames.txt", encoding: .utf8).split(separator: "\n").map { Int($0)! }

// Layout, in frame pixels from the top left: the reference video's 1080p
// dock, doubled. Each icon's 398-pixel canvas holds a 320-pixel body.
let dock = CGRect(x: 1232, y: 706, width: 1376, height: 494), dockRadius = 160.0
let iconCanvas = 398.0, iconY = 954.0, iconX = [1484.0, 1920.0, 2356.0], dotY = 1172.0
let dim: Float = 0.8

func load(_ path: String) -> CGImage {
	let source = CGImageSourceCreateWithURL(URL(fileURLWithPath: path) as CFURL, nil)!
	return CGImageSourceCreateImageAtIndex(source, 0, nil)!
}

struct Layer { let x0: Int, y0: Int, w: Int, h: Int; var px: [UInt8] }

// A premultiplied layer over `rect`, drawn in frame coordinates (y down).
func layer(_ rect: CGRect, _ draw: (CGContext) -> Void) -> Layer {
	let r = rect.integral, w = Int(r.width), h = Int(r.height)
	var px = [UInt8](repeating: 0, count: w * h * 4)
	px.withUnsafeMutableBytes { buf in
		let ctx = CGContext(data: buf.baseAddress, width: w, height: h, bitsPerComponent: 8, bytesPerRow: w * 4,
		                    space: CGColorSpace(name: CGColorSpace.sRGB)!, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
		ctx.interpolationQuality = .high
		ctx.translateBy(x: -r.minX, y: r.maxY)
		ctx.scaleBy(x: 1, y: -1)
		draw(ctx)
	}
	return Layer(x0: Int(r.minX), y0: Int(r.minY), w: w, h: h, px: px)
}

func drawImage(_ ctx: CGContext, _ image: CGImage, _ r: CGRect) {
	ctx.saveGState(); ctx.translateBy(x: r.minX, y: r.maxY); ctx.scaleBy(x: 1, y: -1)
	ctx.draw(image, in: CGRect(origin: .zero, size: r.size)); ctx.restoreGState()
}

func drawLine(_ ctx: CGContext, _ line: CTLine, x: CGFloat, baseline: CGFloat) {
	ctx.saveGState(); ctx.textMatrix = CGAffineTransform(scaleX: 1, y: -1)
	ctx.textPosition = CGPoint(x: x, y: baseline); CTLineDraw(line, ctx); ctx.restoreGState()
}

func text(_ string: String, _ font: CTFont, white: CGFloat, kern: CGFloat = 0) -> CTLine {
	CTLineCreateWithAttributedString(NSAttributedString(string: string, attributes: [
		.font: font, .kern: kern, NSAttributedString.Key(kCTForegroundColorAttributeName as String): CGColor(gray: 1, alpha: white),
	]))
}

// The wordmark's type (ADR 0099) from the repo's own file, never a fallback:
// Funnel Display Medium, tracked −3%.
CTFontManagerRegisterFontsForURL(URL(fileURLWithPath: "\(repo)/app/dmg/fonts/FunnelDisplay[wght].ttf") as CFURL, .process, nil)
func funnelDisplay(size: CGFloat, weight: CGFloat) -> CTFont {
	let wght = 0x7767_6874 // 'wght'
	let descriptor = CTFontDescriptorCreateWithAttributes([
		kCTFontFamilyNameAttribute: "Funnel Display",
		kCTFontVariationAttribute: [wght: weight],
	] as CFDictionary)
	let font = CTFontCreateWithFontDescriptor(descriptor, size, nil)
	precondition(CTFontCopyFamilyName(font) as String == "Funnel Display", "Funnel Display did not load")
	return font
}
let wordmark = text("Pane", funnelDisplay(size: 64, weight: 500), white: 0.95, kern: -0.03 * 64)
let label = text("You choose", NSFont.monospacedSystemFont(ofSize: 52, weight: .regular) as CTFont, white: 0.42)

func iconRect(_ cx: Double) -> CGRect { CGRect(x: cx - iconCanvas / 2, y: iconY - iconCanvas / 2, width: iconCanvas, height: iconCanvas) }

// The dock's soft shadow, cleared under the glass.
let dockPath = CGPath(roundedRect: dock, cornerWidth: dockRadius, cornerHeight: dockRadius, transform: nil)
let shadow = layer(dock.insetBy(dx: -160, dy: -160)) { ctx in
	ctx.setShadow(offset: CGSize(width: 0, height: -20), blur: 70, color: CGColor(gray: 0, alpha: 0.32))
	ctx.addPath(dockPath); ctx.setFillColor(gray: 0, alpha: 1); ctx.fillPath()
	ctx.setShadow(offset: .zero, blur: 0, color: nil)
	ctx.setBlendMode(.clear); ctx.addPath(dockPath); ctx.fillPath()
}

// Finder and Paper, the three running dots, and the header.
let content = layer(CGRect(x: 0, y: 0, width: W, height: 1240)) { ctx in
	drawImage(ctx, load("\(build)/finder.png"), iconRect(iconX[0]))
	drawImage(ctx, load("\(build)/paper.png"), iconRect(iconX[2]))
	ctx.setFillColor(gray: 1, alpha: 0.85)
	for x in iconX { ctx.fillEllipse(in: CGRect(x: x - 8, y: dotY - 8, width: 16, height: 16)) }
	drawLine(ctx, wordmark, x: 144, baseline: 166)
	let labelWidth = CGFloat(CTLineGetTypographicBounds(label, nil, nil, nil))
	drawLine(ctx, label, x: CGFloat(W) - 144 - labelWidth, baseline: 166)
}

// Per sky, Pane's icon in the middle, as the app's Dock shows it.
let skyLayers = skies.map { sky in
	layer(CGRect(x: 1700, y: 740, width: 440, height: 760)) { ctx in
		drawImage(ctx, load("\(repo)/app/Pane/Assets.xcassets/AppIcon\(sky).imageset/AppIcon\(sky).png"), iconRect(iconX[1]))
	}
}

// The glass, precomputed per pixel: its coverage, where it samples the blurred
// backdrop (pushed outward near the rim, as Apple's lens bends what is behind
// it), and the light added on top (the rim's specular, brightest facing the
// top left and bottom right, and a glow under the top edge). The frost
// (blur, saturation, a white wash) follows the Paper file "Liquid Glass".
let band = 70.0, bend = 46.0, rim = 3.5, rimBase = 0.22, rimSpec = 0.6, glow = 0.10
let kernel: UInt32 = 81, saturation: Float = 1.45, wash: Float = 0.2
let glassBox = dock.insetBy(dx: -2, dy: -2).integral, gw = Int(glassBox.width), gh = Int(glassBox.height)
let blurBox = dock.insetBy(dx: -CGFloat(kernel) * 1.6 - 60, dy: -CGFloat(kernel) * 1.6 - 60).integral
var coverage = [Float](repeating: 0, count: gw * gh), sampleX = coverage, sampleY = coverage, light = coverage
do {
	let cx = dock.midX, cy = dock.midY, bx = dock.width / 2 - dockRadius, by = dock.height / 2 - dockRadius
	let lx = -1 / 2.0.squareRoot(), ly = -1 / 2.0.squareRoot()
	for j in 0..<gh { for i in 0..<gw {
		let px = glassBox.minX + Double(i) + 0.5, py = glassBox.minY + Double(j) + 0.5
		let dx = px - cx, dy = py - cy, qx = abs(dx) - bx, qy = abs(dy) - by
		let ox = max(qx, 0), oy = max(qy, 0)
		let d = (ox * ox + oy * oy).squareRoot() + min(max(qx, qy), 0) - dockRadius
		let k = j * gw + i
		coverage[k] = Float(min(max(0.5 - d, 0), 1))
		if coverage[k] == 0 { continue }
		var nx = 0.0, ny = 0.0
		if qx > 0 && qy > 0 { let l = (qx * qx + qy * qy).squareRoot(); nx = qx / l; ny = qy / l }
		else if qx > qy { nx = 1 } else { ny = 1 }
		nx *= dx < 0 ? -1 : 1; ny *= dy < 0 ? -1 : 1
		let depth = max(-d, 0)
		var offset = 0.0
		if depth < band { let t = 1 - depth / band; offset = bend * t * t }
		sampleX[k] = Float(px + nx * offset - blurBox.minX - 0.5); sampleY[k] = Float(py + ny * offset - blurBox.minY - 0.5)
		var a = 0.0
		if depth < rim { let spec = abs(nx * lx + ny * ly); a += (rimBase + rimSpec * spec * spec) * (1 - depth / rim) }
		if depth < 36 && ny < 0 { let t = 1 - depth / 36; a += glow * t * t * -ny }
		light[k] = Float(a * 255)
	} }
}

func drawGlass(_ frame: UnsafeMutablePointer<UInt8>, _ clean: UnsafePointer<UInt8>) {
	let bw = Int(blurBox.width), bh = Int(blurBox.height), bx0 = Int(blurBox.minX), by0 = Int(blurBox.minY)
	var a = [UInt8](repeating: 0, count: bw * bh * 4), b = a
	for j in 0..<bh { memcpy(&a[j * bw * 4], clean + ((by0 + j) * W + bx0) * 4, bw * 4) }
	// Three box passes, close to a Gaussian.
	a.withUnsafeMutableBytes { pa in b.withUnsafeMutableBytes { pb in
		var va = vImage_Buffer(data: pa.baseAddress, height: vImagePixelCount(bh), width: vImagePixelCount(bw), rowBytes: bw * 4)
		var vb = vImage_Buffer(data: pb.baseAddress, height: vImagePixelCount(bh), width: vImagePixelCount(bw), rowBytes: bw * 4)
		for _ in 0..<3 {
			vImageBoxConvolve_ARGB8888(&va, &vb, nil, 0, 0, kernel, kernel, nil, vImage_Flags(kvImageEdgeExtend))
			swap(&va, &vb)
		}
		if va.data != pa.baseAddress { memcpy(pa.baseAddress, va.data, bw * bh * 4) }
	} }
	a.withUnsafeBufferPointer { blur in
		for j in 0..<gh { for i in 0..<gw {
			let k = j * gw + i, c = coverage[k]
			if c == 0 { continue }
			let fx = sampleX[k], fy = sampleY[k]
			let x0 = min(max(Int(fx), 0), bw - 2), y0 = min(max(Int(fy), 0), bh - 2)
			let tx = min(max(fx - Float(x0), 0), 1), ty = min(max(fy - Float(y0), 0), 1)
			let i00 = (y0 * bw + x0) * 4, i10 = i00 + 4, i01 = i00 + bw * 4, i11 = i01 + 4
			let w00 = (1 - tx) * (1 - ty), w10 = tx * (1 - ty), w01 = (1 - tx) * ty, w11 = tx * ty
			let r = Float(blur[i00]) * w00 + Float(blur[i10]) * w10 + Float(blur[i01]) * w01 + Float(blur[i11]) * w11
			let g = Float(blur[i00 + 1]) * w00 + Float(blur[i10 + 1]) * w10 + Float(blur[i01 + 1]) * w01 + Float(blur[i11 + 1]) * w11
			let bl = Float(blur[i00 + 2]) * w00 + Float(blur[i10 + 2]) * w10 + Float(blur[i01 + 2]) * w01 + Float(blur[i11 + 2]) * w11
			let lum = 0.2126 * r + 0.7152 * g + 0.0722 * bl
			let o = ((Int(glassBox.minY) + j) * W + Int(glassBox.minX) + i) * 4, add = light[k]
			@inline(__always) func put(_ channel: Int, _ v0: Float) {
				let v = min(max((lum + (v0 - lum) * saturation) * (1 - wash) + 255 * wash + add, 0), 255)
				frame[o + channel] = UInt8(Float(frame[o + channel]) * (1 - c) + v * c + 0.5)
			}
			put(0, r); put(1, g); put(2, bl)
		} }
	}
}

func over(_ l: Layer, _ frame: UnsafeMutablePointer<UInt8>) {
	l.px.withUnsafeBufferPointer { src in
		for j in 0..<l.h { for i in 0..<l.w {
			let s = (j * l.w + i) * 4, sa = src[s + 3]
			if sa == 0 { continue }
			let o = ((l.y0 + j) * W + l.x0 + i) * 4, inv = 255 - Int(sa)
			for channel in 0..<3 { frame[o + channel] = UInt8(min(255, Int(src[s + channel]) + (Int(frame[o + channel]) * inv + 127) / 255)) }
		} }
	}
}

var dimmed = [UInt8](repeating: 0, count: 256)
for v in 0..<256 { dimmed[v] = UInt8(Float(v) * dim + 0.5) }

let frame = UnsafeMutablePointer<UInt8>.allocate(capacity: frameBytes)
let clean = UnsafeMutablePointer<UInt8>.allocate(capacity: frameBytes)
var n = 0
while fread(clean, 1, frameBytes, stdin) == frameBytes {
	dimmed.withUnsafeBufferPointer { t in
		for p in stride(from: 0, to: frameBytes, by: 4) {
			frame[p] = t[Int(clean[p])]; frame[p + 1] = t[Int(clean[p + 1])]; frame[p + 2] = t[Int(clean[p + 2])]; frame[p + 3] = 255
		}
	}
	over(shadow, frame)
	drawGlass(frame, clean)
	over(content, frame)
	over(skyLayers[order[min(n, order.count - 1)]], frame)
	fwrite(frame, 1, frameBytes, stdout)
	n += 1
}
