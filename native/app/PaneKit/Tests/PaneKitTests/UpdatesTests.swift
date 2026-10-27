import Foundation
import PaneKit
import Testing

private func release(tag: String, assets: [String]) -> Data {
	let list = assets.map { #"{"name":"\#($0)","browser_download_url":"https://github.com/andredezzy/pane/releases/download/\#(tag)/\#($0)"}"# }

	return Data(#"{"tag_name":"\#(tag)","published_at":"2026-10-20T12:00:00Z","assets":[\#(list.joined(separator: ","))]}"#.utf8)
}

@Suite("Updates")
struct UpdatesTests {
	@Test("a newer release with the arm64 dmg is the available update")
	func newer() throws {
		let update = try Updates.available(
			inRelease: release(tag: "v0.3.0", assets: ["Pane-0.3.0-x64.dmg", "Pane-0.3.0-arm64.dmg", "Pane-0.3.0.exe"]),
			currentVersion: "0.2.4"
		)

		#expect(update?.version == "0.3.0")
		#expect(update?.dmgURL.lastPathComponent == "Pane-0.3.0-arm64.dmg")
		#expect(update?.publishedAt == "2026-10-20T12:00:00Z")
	}

	@Test("the same or an older release is none")
	func notNewer() throws {
		#expect(try Updates.available(inRelease: release(tag: "v0.2.4", assets: ["Pane-0.2.4-arm64.dmg"]), currentVersion: "0.2.4") == nil)
		#expect(try Updates.available(inRelease: release(tag: "v0.2.3", assets: ["Pane-0.2.3-arm64.dmg"]), currentVersion: "0.2.4") == nil)
	}

	@Test("a release without the arm64 dmg is none")
	func noDmg() throws {
		#expect(try Updates.available(inRelease: release(tag: "v1.0.0", assets: ["Pane-1.0.0-x64.dmg"]), currentVersion: "0.2.4") == nil)
	}

	@Test("a tag without its v counts too")
	func bareTag() throws {
		#expect(try Updates.available(inRelease: release(tag: "1.0.0", assets: ["Pane-1.0.0-arm64.dmg"]), currentVersion: "0.2.4")?.version == "1.0.0")
	}

	@Test("JSON that is not a release, or a version that does not parse, fails the check")
	func failures() {
		#expect(throws: (any Error).self) { try Updates.available(inRelease: Data(#"{"message":"rate limited"}"#.utf8), currentVersion: "0.2.4") }
		#expect(throws: (any Error).self) { try Updates.available(inRelease: release(tag: "vnext", assets: ["Pane-next-arm64.dmg"]), currentVersion: "0.2.4") }
	}

	// format-eta.ts's buckets.
	@Test("the time left is a coarse bucket")
	func eta() {
		#expect(Updates.formatETA(seconds: 0) == "<1min")
		#expect(Updates.formatETA(seconds: 44) == "<1min")
		#expect(Updates.formatETA(seconds: 45) == "~1min")
		#expect(Updates.formatETA(seconds: 150) == "~3min")
		#expect(Updates.formatETA(seconds: 3600) == "~60min")
	}
}

@Suite("DownloadETA")
struct DownloadETATests {
	private let start = Date(timeIntervalSince1970: 1_800_000_000)

	@Test("uses the whole transfer's average rate")
	func average() {
		var eta = DownloadETA(startedAt: start)

		// 10 MB of 100 MB in 10s: 1 MB/s, 90s left.
		#expect(eta.update(received: 10_000_000, total: 100_000_000, now: start + 10) == 90)
	}

	@Test("a shorter estimate shows at once; a longer one only after five seconds")
	func hysteresis() {
		var eta = DownloadETA(startedAt: start)

		#expect(eta.update(received: 50_000_000, total: 100_000_000, now: start + 10) == 10)
		// A stall: the average drops, the estimate rises, and is held.
		#expect(eta.update(received: 50_000_000, total: 100_000_000, now: start + 20) == 10)
		#expect(eta.update(received: 50_000_000, total: 100_000_000, now: start + 24) == 10)
		#expect(eta.update(received: 50_000_000, total: 100_000_000, now: start + 26) == 26)
		// Faster again: shorter at once.
		#expect(eta.update(received: 90_000_000, total: 100_000_000, now: start + 27) == 3)
	}

	@Test("no estimate before the size or the rate is known")
	func unknown() {
		var eta = DownloadETA(startedAt: start)

		#expect(eta.update(received: 1000, total: 0, now: start + 1) == nil)
		#expect(eta.update(received: 0, total: 1000, now: start + 1) == nil)
	}
}
