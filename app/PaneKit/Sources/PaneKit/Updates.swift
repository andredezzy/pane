import Foundation

// The update check (ADR 0002, and the Electron app's updater.ts, whose
// contract every Electron Pane still reads): the repo's latest GitHub
// Release, newer than the running app and carrying the arm64 dmg, is the one
// available update. Installing stays the user's: Pane downloads the dmg and
// opens it.
public struct AvailableUpdate: Codable, Hashable, Sendable {
	public var version: String
	public var dmgURL: URL
	public var publishedAt: String
	// The dmg's SHA-256 in lowercase hex, as the release gives it.
	public var sha256: String?

	public init(version: String, dmgURL: URL, publishedAt: String, sha256: String? = nil) {
		self.version = version
		self.dmgURL = dmgURL
		self.publishedAt = publishedAt
		self.sha256 = sha256
	}
}

public enum Updates {
	public static let releasesURL = URL(string: "https://api.github.com/repos/andredezzy/pane/releases/latest")!
	public static let userAgent = "Pane-Updater"
	public static let dmgSuffix = "-arm64.dmg"
	// A completed check runs again six hours later, as Chrome's and Brave's
	// check within hours, so a security release reaches Pane soon; a failed
	// one sooner.
	public static let checkInterval: TimeInterval = 6 * 60 * 60
	public static let retryDelays: [TimeInterval] = [5 * 60, 15 * 60, 60 * 60]
	// The checking state holds at least this long, so a click visibly does
	// something even when the answer comes at once.
	public static let minimumChecking: TimeInterval = 0.8

	private struct Release: Decodable {
		struct Asset: Decodable {
			let name: String
			let browserDownloadURL: URL
			let digest: String?

			private enum CodingKeys: String, CodingKey {
				case name
				case browserDownloadURL = "browser_download_url"
				case digest
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

	// The update in GitHub's latest-release JSON (fetched from `releasesURL`),
	// or nil when it is not newer than `currentVersion`, or has no dmg Pane
	// may download. Throws on JSON that is not a release, or a version that
	// does not parse.
	public static func available(inRelease json: Data, currentVersion: String, from releasesURL: URL = releasesURL) throws -> AvailableUpdate? {
		let release = try JSONDecoder().decode(Release.self, from: json)
		let version = release.tagName.hasPrefix("v") || release.tagName.hasPrefix("V")
			? String(release.tagName.dropFirst())
			: release.tagName

		guard
			let asset = release.assets.first(where: { $0.name.hasSuffix(dmgSuffix) }),
			isTrustedDownload(asset.browserDownloadURL, from: releasesURL),
			try isNewerVersion(version, than: currentVersion)
		else {
			return nil
		}

		return AvailableUpdate(
			version: version,
			dmgURL: asset.browserDownloadURL,
			publishedAt: release.publishedAt,
			sha256: sha256(fromDigest: asset.digest)
		)
	}

	// The hex of an asset's digest, which GitHub gives as "sha256:<hex>", or
	// nil for none or another algorithm.
	public static func sha256(fromDigest digest: String?) -> String? {
		guard let digest, digest.hasPrefix("sha256:") else {
			return nil
		}

		let hex = digest.dropFirst("sha256:".count).lowercased()

		return hex.count == 64 && hex.allSatisfy(\.isHexDigit) ? hex : nil
	}

	// The repo's own release downloads: what a link from GitHub's JSON must
	// name, since Pane opens the file it fetches. TLS vouches for the JSON,
	// not for where its links go.
	public static let downloadsPrefix = "/andredezzy/pane/releases/download/"

	// Whether Pane may download and open the dmg at `url`: over https, from
	// the repo's releases on github.com, under a plain file name ending in
	// the dmg's suffix. A stand-in release server (PANE_UPDATE_URL, the
	// scenarios') serves its downloads from its own host.
	public static func isTrustedDownload(_ url: URL, from releasesURL: URL = releasesURL) -> Bool {
		let name = url.lastPathComponent

		guard name.hasSuffix(dmgSuffix), !name.hasPrefix("."), !name.contains("/") else {
			return false
		}

		guard releasesURL == Self.releasesURL else {
			return url.scheme == releasesURL.scheme && url.host() == releasesURL.host()
		}

		return url.scheme == "https"
			&& url.host() == "github.com"
			&& url.port == nil
			&& url.standardized.path(percentEncoded: false).hasPrefix(downloadsPrefix)
			&& !url.path(percentEncoded: false).contains("/../")
	}

	// A few coarse buckets rather than a live countdown (format-eta.ts): "~3min"
	// can only step to "~2min".
	public static func formatETA(seconds: Int) -> String {
		seconds < 45 ? "<1min" : "~\(max(1, Int((Double(seconds) / 60).rounded())))min"
	}
}

// When a download leaves its connection (ADR 0002). On a path that throttles
// some connections and not others, one that starts slow stays slow while the
// next gets the full rate. Slower than 1 MB/s over its window, a connection is
// left. Each new one that is no faster doubles the next window, and after four
// the download stays put: the link itself is the slow one then.
public struct DownloadPace: Sendable {
	private static let slowBytesPerSecond = 1_000_000.0
	private static let firstWindow: TimeInterval = 5
	private static let switchesInARow = 4

	private var switches = 0
	private var windowStart: Date
	private var windowBytes: Int64 = 0

	public init(now: Date) {
		windowStart = now
	}

	// How long the connection gets before it is judged.
	public var window: TimeInterval {
		Self.firstWindow * pow(2, Double(switches))
	}

	// A connection's window starts: a new one, then again at its first bytes,
	// since its setup (DNS, TLS, a redirect) says nothing of its rate.
	public mutating func begin(received: Int64, now: Date) {
		windowStart = now
		windowBytes = received
	}

	// What is left of the window.
	public func timeLeft(now: Date) -> TimeInterval {
		window - now.timeIntervalSince(windowStart)
	}

	// At the end of its window, whether to leave the connection, one that can be
	// left: one that cannot spends no switch. The next window starts here either
	// way.
	public mutating func shouldSwitch(received: Int64, now: Date, canLeave: Bool = true) -> Bool {
		let elapsed = now.timeIntervalSince(windowStart)
		let rate = elapsed > 0 ? Double(received - windowBytes) / elapsed : 0
		begin(received: received, now: now)

		guard rate < Self.slowBytesPerSecond else {
			switches = 0

			return false
		}

		guard canLeave, switches < Self.switchesInARow else {
			return false
		}

		switches += 1

		return true
	}
}

// A download's time left (updater.ts): the average rate since the first
// bytes, which bursts and stalls cannot spike, with hysteresis on top. None
// in the first second: the connection's setup would weigh on the rate, and a
// 160 MB dmg started at "~150min". Then a shorter estimate shows at once; a
// longer one only once it held for five seconds.
public struct DownloadETA: Sendable {
	private static let settle: TimeInterval = 1
	private static let increaseHold: TimeInterval = 5

	private var firstBytesAt: Date?
	private var firstBytes: Int64 = 0
	private var published: Int?
	private var increaseSince: Date?

	public init() {}

	// Seconds left after `received` of `total` bytes at `now`, or nil until
	// the rate is known.
	public mutating func update(received: Int64, total: Int64, now: Date) -> Int? {
		guard total > 0, received > 0 else {
			return published
		}

		guard let firstBytesAt else {
			firstBytesAt = now
			firstBytes = received

			return published
		}

		let elapsed = now.timeIntervalSince(firstBytesAt)

		guard elapsed >= Self.settle else {
			return published
		}

		let rate = Double(received - firstBytes) / elapsed

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
