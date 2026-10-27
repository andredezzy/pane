import Foundation

// The update check (ADR 0002, apps/veil updater.ts): the repo's latest GitHub
// Release, newer than the running app and carrying the arm64 dmg, is the one
// available update. Installing stays the user's: Pane downloads the dmg and
// opens it.
public struct AvailableUpdate: Codable, Hashable, Sendable {
	public var version: String
	public var dmgURL: URL
	public var publishedAt: String

	public init(version: String, dmgURL: URL, publishedAt: String) {
		self.version = version
		self.dmgURL = dmgURL
		self.publishedAt = publishedAt
	}
}

public enum Updates {
	public static let releasesURL = URL(string: "https://api.github.com/repos/andredezzy/pane/releases/latest")!
	public static let userAgent = "Pane-Updater"
	public static let dmgSuffix = "-arm64.dmg"
	// A completed check runs again a day later; a failed one sooner.
	public static let checkInterval: TimeInterval = 24 * 60 * 60
	public static let retryDelays: [TimeInterval] = [5 * 60, 15 * 60, 60 * 60]
	// The checking state holds at least this long, so a click visibly does
	// something even when the answer comes at once.
	public static let minimumChecking: TimeInterval = 0.8

	private struct Release: Decodable {
		struct Asset: Decodable {
			let name: String
			let browserDownloadURL: URL

			private enum CodingKeys: String, CodingKey {
				case name
				case browserDownloadURL = "browser_download_url"
			}
		}

		let tagName: String
		let publishedAt: String
		let assets: [Asset]

		private enum CodingKeys: String, CodingKey {
			case tagName = "tag_name"
			case publishedAt = "published_at"
			case assets
		}
	}

	// The update in GitHub's latest-release JSON, or nil when it is not newer
	// than `currentVersion` or has no dmg. Throws on JSON that is not a
	// release, or a version that does not parse.
	public static func available(inRelease json: Data, currentVersion: String) throws -> AvailableUpdate? {
		let release = try JSONDecoder().decode(Release.self, from: json)
		let version = release.tagName.hasPrefix("v") || release.tagName.hasPrefix("V")
			? String(release.tagName.dropFirst())
			: release.tagName

		guard
			let asset = release.assets.first(where: { $0.name.hasSuffix(dmgSuffix) }),
			try isNewerVersion(version, than: currentVersion)
		else {
			return nil
		}

		return AvailableUpdate(version: version, dmgURL: asset.browserDownloadURL, publishedAt: release.publishedAt)
	}

	// A few coarse buckets rather than a live countdown (format-eta.ts): "~3min"
	// can only step to "~2min".
	public static func formatETA(seconds: Int) -> String {
		seconds < 45 ? "<1min" : "~\(max(1, Int((Double(seconds) / 60).rounded())))min"
	}
}

// A download's time left (updater.ts): the whole transfer's average rate,
// which bursts and stalls cannot spike, with hysteresis on top. A shorter
// estimate shows at once; a longer one only once it held for five seconds.
public struct DownloadETA: Sendable {
	private static let increaseHold: TimeInterval = 5

	private let startedAt: Date
	private var published: Int?
	private var increaseSince: Date?

	public init(startedAt: Date) {
		self.startedAt = startedAt
	}

	// Seconds left after `received` of `total` bytes at `now`, or nil until
	// the rate is known.
	public mutating func update(received: Int64, total: Int64, now: Date) -> Int? {
		guard total > 0 else {
			return published
		}

		let elapsed = now.timeIntervalSince(startedAt)
		let rate = elapsed > 0 ? Double(received) / elapsed : 0

		guard rate > 0 else {
			return published
		}

		let raw = max(0, Int((Double(total - received) / rate).rounded()))

		if published == nil || raw <= published! {
			published = raw
			increaseSince = nil
		} else if let since = increaseSince {
			if now.timeIntervalSince(since) > Self.increaseHold {
				published = raw
				increaseSince = nil
			}
		} else {
			increaseSince = now
		}

		return published
	}
}
