import Foundation
import PaneKit
import Testing

private func release(tag: String, assets: [String], downloads: String = "https://github.com/andredezzy/pane/releases/download", digest: String? = nil) -> Data {
	let digestField = digest.map { #","digest":"\#($0)""# } ?? ""
	let list = assets.map { #"{"name":"\#($0)","browser_download_url":"\#(downloads)/\#(tag)/\#($0)"\#(digestField)}"# }

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

	@Test("a dmg Pane may not download is no update")
	func untrustedDownloads() throws {
		for downloads in [
			"http://github.com/andredezzy/pane/releases/download",
			"https://evil.example/andredezzy/pane/releases/download",
			"https://github.com/someone/else/releases/download",
			"https://github.com/andredezzy/pane/releases/download/../../../someone/else/releases/download",
			"https://github.com:8443/andredezzy/pane/releases/download",
		] {
			#expect(try Updates.available(inRelease: release(tag: "v1.0.0", assets: ["Pane-1.0.0-arm64.dmg"], downloads: downloads), currentVersion: "0.2.4") == nil, "\(downloads)")
		}

		#expect(!Updates.isTrustedDownload(URL(string: "https://github.com/andredezzy/pane/releases/download/v1/-arm64.dmg/..")!))
		#expect(!Updates.isTrustedDownload(URL(string: "https://github.com/andredezzy/pane/releases/download/v1/.hidden-arm64.dmg")!))
	}

	@Test("a stand-in release server serves its downloads from its own host")
	func standIn() throws {
		let server = URL(string: "http://127.0.0.1:4100/releases/latest")!
		let local = release(tag: "v1.0.0", assets: ["Pane-1.0.0-arm64.dmg"], downloads: "http://127.0.0.1:4200/releases/download")

		#expect(try Updates.available(inRelease: local, currentVersion: "0.2.4", from: server)?.version == "1.0.0")
		#expect(try Updates.available(inRelease: local, currentVersion: "0.2.4") == nil)
	}

	@Test("the dmg's SHA-256 comes from its digest, when GitHub gives one")
	func digest() throws {
		let hex = String(repeating: "ab", count: 32)
		let dmg = ["Pane-1.0.0-arm64.dmg"]

		#expect(try Updates.available(inRelease: release(tag: "v1.0.0", assets: dmg, digest: "sha256:\(hex)"), currentVersion: "0.2.4")?.sha256 == hex)
		#expect(try Updates.available(inRelease: release(tag: "v1.0.0", assets: dmg, digest: "sha256:\(hex.uppercased())"), currentVersion: "0.2.4")?.sha256 == hex)
		#expect(try Updates.available(inRelease: release(tag: "v1.0.0", assets: dmg), currentVersion: "0.2.4")?.sha256 == nil)
		#expect(Updates.sha256(fromDigest: "sha512:\(hex)") == nil)
		#expect(Updates.sha256(fromDigest: "sha256:\(hex.dropLast())") == nil)
		#expect(Updates.sha256(fromDigest: "sha256:\(hex.dropLast(2))zz") == nil)
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

	@Test("uses the average rate since the first bytes")
	func average() {
		var eta = DownloadETA()

		// The first bytes, after the connection's setup, start the clock.
		#expect(eta.update(received: 1_000_000, total: 100_000_000, now: start) == nil)
		// 10 MB more in 10s: 1 MB/s, 89s left.
		#expect(eta.update(received: 11_000_000, total: 100_000_000, now: start + 10) == 89)
	}

	@Test("none in the first second, however slow the start")
	func settles() {
		var eta = DownloadETA()

		#expect(eta.update(received: 16_384, total: 163_000_000, now: start) == nil)
		#expect(eta.update(received: 32_768, total: 163_000_000, now: start + 0.5) == nil)
		#expect(eta.update(received: 15_032_768, total: 163_000_000, now: start + 1) == 10)
	}

	@Test("a shorter estimate shows at once; a longer one only after five seconds")
	func hysteresis() {
		var eta = DownloadETA()

		#expect(eta.update(received: 1, total: 100_000_001, now: start) == nil)
		#expect(eta.update(received: 50_000_001, total: 100_000_001, now: start + 10) == 10)
		// A stall: the average drops, the estimate rises, and is held.
		#expect(eta.update(received: 50_000_001, total: 100_000_001, now: start + 20) == 10)
		#expect(eta.update(received: 50_000_001, total: 100_000_001, now: start + 24) == 10)
		#expect(eta.update(received: 50_000_001, total: 100_000_001, now: start + 26) == 26)
		// Faster again: shorter at once.
		#expect(eta.update(received: 90_000_001, total: 100_000_001, now: start + 27) == 3)
	}

	@Test("no estimate before the size or the rate is known")
	func unknown() {
		var eta = DownloadETA()

		#expect(eta.update(received: 1000, total: 0, now: start + 1) == nil)
		#expect(eta.update(received: 0, total: 1000, now: start + 1) == nil)
		#expect(eta.update(received: 500, total: 1000, now: start + 2) == nil)
		#expect(eta.update(received: 500, total: 1000, now: start + 4) == nil)
	}
}

@Suite("UpdateInstall")
struct UpdateInstallTests {
	@Test("installs over a bundle Pane runs from in place, never a translocated or mounted one")
	func target() {
		#expect(UpdateInstall.target(bundlePath: "/Applications/Pane.app") == "/Applications/Pane.app")
		#expect(UpdateInstall.target(bundlePath: "/Users/me/Applications/Pane.app") == "/Users/me/Applications/Pane.app")
		#expect(UpdateInstall.target(bundlePath: "/private/var/folders/x/T/AppTranslocation/1234/d/Pane.app") == nil)
		#expect(UpdateInstall.target(bundlePath: "/Volumes/Pane/Pane.app") == nil)
		#expect(UpdateInstall.target(bundlePath: "/usr/local/bin/pane") == nil)
	}

	@Test("stages the new version hidden beside the app, and knows its leftovers")
	func staging() {
		#expect(UpdateInstall.stagedName(version: "0.9.0") == ".Pane-0.9.0.app")
		#expect(UpdateInstall.isLeftover(".Pane-0.9.0.app"))
		#expect(!UpdateInstall.isLeftover("Pane.app"))
		#expect(!UpdateInstall.isLeftover(".Pane-notes.txt"))
	}

	@Test("mounts a dmg at a folder named for its process, and knows one whose process is gone")
	func mounts() {
		let running: (Int32) -> Bool = { $0 == 42 }
		let mine = UpdateInstall.mountName(pid: 42)

		#expect(mine.hasPrefix("pane-update-42-"))
		#expect(!UpdateInstall.isLeftoverMount(mine, isRunning: running))
		#expect(UpdateInstall.isLeftoverMount(UpdateInstall.mountName(pid: 7), isRunning: running))
		// Named before 0.13.1, without a pid.
		#expect(UpdateInstall.isLeftoverMount("pane-update-19CDC9D9-4317-4BB6-B467-A974ACADFDC1", isRunning: running))
		#expect(!UpdateInstall.isLeftoverMount("pane-update-notes", isRunning: running))
		#expect(!UpdateInstall.isLeftoverMount("pane-update-7-notes", isRunning: running))
		#expect(!UpdateInstall.isLeftoverMount("xyz.roundedcorners.pane.1pSFam", isRunning: running))
	}
}
