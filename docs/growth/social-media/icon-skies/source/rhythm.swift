// The video's rhythm and its sound: which sky each of the 600 frames shows,
// and the click on every swap with the boom on the landing.
// Usage: swift rhythm.swift <build dir>   → frames.txt, audio.wav
//
// The swaps accelerate: a geometric ramp of frame intervals, floored at 3
// frames (20 swaps a second), with a swap count that is a multiple of 10, so
// the last swap lands on Day. Everything is synthesized, no samples: a swap
// is a low knock that rises in pitch as the swaps speed up, over a 2.4 kHz
// click; the landing is a boom falling from 90 to 45 Hz under a burst of air.
import Foundation

let build = URL(fileURLWithPath: CommandLine.arguments[1])
let fps = 60, total = 600, start = 30, land = 552
let rate = 48000

var best: (error: Int, cuts: [Int])?
for r1000 in 930..<990 {
	let r = Double(r1000) / 1000
	for first in 18..<26 {
		var cuts = [start], f = start, k = 0
		while true {
			f += max(3, Int((Double(first) * pow(r, Double(k))).rounded())); k += 1
			if f > land + 6 { break }
			cuts.append(f)
		}
		while cuts.count % 10 != 0 { cuts.removeLast() }
		let error = abs(cuts.last! - land)
		if best == nil || error < best!.error { best = (error, cuts) }
	}
}
let cuts = best!.cuts
precondition(cuts.last == land, "the last swap must land on frame \(land)")

var frames: [Int] = [], sky = 0
let cutSet = Set(cuts)
for i in 0..<total {
	if cutSet.contains(i) { sky = (sky + 1) % 10 }
	frames.append(sky)
}
try! frames.map(String.init).joined(separator: "\n").write(to: build.appendingPathComponent("frames.txt"), atomically: true, encoding: .utf8)

var audio = [Double](repeating: 0, count: rate * total / fps)
func add(_ t0: Double, _ duration: Double, _ sound: (Double) -> Double) {
	let s0 = Int(t0 * Double(rate))
	for n in 0..<Int(duration * Double(rate)) where s0 + n < audio.count {
		audio[s0 + n] += sound(Double(n) / Double(rate))
	}
}

for j in 0..<(cuts.count - 1) {
	let p = Double(j) / Double(cuts.count - 1)
	let gap = Double(cuts[j + 1] - cuts[j]) / Double(fps)
	let pitch = 140 + 200 * p * p, decay = min(0.02, gap * 0.35), gain = 0.45 + 0.25 * p
	add(Double(cuts[j]) / Double(fps), 0.12) { t in
		gain * (0.7 * sin(2 * .pi * pitch * t) * exp(-t / decay) + 0.3 * sin(2 * .pi * 2400 * t) * exp(-t / 0.0015))
	}
}

var seed: UInt64 = 1, air = 0.0
func noise() -> Double {
	seed = seed &* 6_364_136_223_846_793_005 &+ 1_442_695_040_888_963_407
	return Double(seed >> 11) / Double(1 << 53) * 2 - 1
}
add(Double(land) / Double(fps), 1.2) { t in
	air += 0.08 * (noise() - air)
	let phase = 2 * .pi * (45 * t + 45 / 6 * (1 - exp(-6 * t)))
	return sin(phase) * exp(-t / 0.45) + 2.2 * air * exp(-t / 0.06) + 0.35 * sin(2 * .pi * 2400 * t) * exp(-t / 0.002)
}

// 16-bit stereo WAV, its peak at −1 dBFS.
let peak = audio.map(abs).max()!
var wav = Data()
func put<T>(_ value: T) { withUnsafeBytes(of: value) { wav.append(contentsOf: $0) } }
let bytes = audio.count * 4
wav.append(contentsOf: Array("RIFF".utf8)); put(UInt32(36 + bytes).littleEndian)
wav.append(contentsOf: Array("WAVEfmt ".utf8)); put(UInt32(16).littleEndian); put(UInt16(1).littleEndian); put(UInt16(2).littleEndian)
put(UInt32(rate).littleEndian); put(UInt32(rate * 4).littleEndian); put(UInt16(4).littleEndian); put(UInt16(16).littleEndian)
wav.append(contentsOf: Array("data".utf8)); put(UInt32(bytes).littleEndian)
for x in audio {
	let v = Int16(x / peak * 0.89 * 32767).littleEndian
	put(v); put(v)
}
try! wav.write(to: build.appendingPathComponent("audio.wav"))
