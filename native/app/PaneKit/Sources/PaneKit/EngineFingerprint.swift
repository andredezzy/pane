import Foundation

// What the engine applies for a fingerprinted profile (PaneBrowserApplication's
// setFingerprint:forContextIdentifier:): the User-Agent, its Client Hints, and
// the languages the browser side presents, and the values each of the
// profile's renderers reports (apps/veil profile/fingerprint-preload.ts, which
// spoofed the same surfaces from script).
public struct EngineFingerprint: Codable, Hashable, Sendable {
	// blink::UserAgentMetadata.
	public struct Metadata: Codable, Hashable, Sendable {
		public var brands: [UABrand]
		public var fullVersionList: [UABrand]
		public var fullVersion: String
		public var platform: String
		public var platformVersion: String
		public var architecture: String
		public var bitness: String
		public var model: String
		public var mobile: Bool
		public var wow64: Bool
		public var formFactors: [String]
	}

	public struct Screen: Codable, Hashable, Sendable {
		public var width: Int
		public var height: Int
		public var availWidth: Int
		public var availHeight: Int
		public var availLeft: Int
		public var availTop: Int
		public var colorDepth: Int
	}

	// What Blink reports in every frame and worker of the profile.
	public struct Renderer: Codable, Hashable, Sendable {
		public var platform: String
		public var hardwareConcurrency: Int
		public var deviceMemory: Double
		public var maxTouchPoints: Int
		public var screen: Screen
		public var devicePixelRatio: Double
		public var webglVendor: String?
		public var webglRenderer: String?
		public var canvasNoise: Bool
		public var audioNoise: Bool
		// Seeds the canvas and audio noise, so it is stable per profile.
		public var noiseSeed: UInt32
		public var timezone: String?
		// The default locale of Intl.
		public var locale: String
		// The host's voices (macOS ones) would contradict another platform.
		public var hideSpeechVoices: Bool
	}

	public var userAgent: String
	public var userAgentMetadata: Metadata
	public var languages: [String]
	public var renderer: Renderer

	// `engineUserAgent` and `engineVersion` are the engine's own
	// (BrowserApplication's userAgent and chromiumVersion): the claimed Chrome
	// version always follows the engine.
	public init(fingerprint: Fingerprint, profileID: String, engineUserAgent: String, engineVersion: String) {
		var reconciled = fingerprint
		reconciled.userAgent = reconcileChromeVersion(fingerprint.userAgent, realUA: engineUserAgent)
		let hints = reconciled.clientHints(engineVersion: engineVersion)

		userAgent = reconciled.userAgent
		userAgentMetadata = Metadata(
			brands: hints.brands,
			fullVersionList: hints.fullVersionList,
			fullVersion: hints.uaFullVersion,
			platform: hints.platform,
			platformVersion: hints.platformVersion,
			architecture: hints.architecture,
			bitness: hints.bitness,
			model: "",
			mobile: hints.mobile,
			wow64: false,
			formFactors: ["Desktop"]
		)
		languages = fingerprint.languages.isEmpty ? [fingerprint.language] : fingerprint.languages
		renderer = Renderer(
			platform: fingerprint.navigatorPlatform,
			hardwareConcurrency: fingerprint.hardwareConcurrency,
			deviceMemory: Self.approximatedDeviceMemory(fingerprint.deviceMemory),
			maxTouchPoints: fingerprint.maxTouchPoints,
			screen: Self.screen(fingerprint),
			devicePixelRatio: fingerprint.platform == .macOS ? 2 : 1,
			webglVendor: fingerprint.webgl?.vendor,
			webglRenderer: fingerprint.webgl?.renderer,
			canvasNoise: fingerprint.canvas.noise,
			audioNoise: fingerprint.audio.noise,
			noiseSeed: Self.noiseSeed(profileID),
			timezone: fingerprint.timezone.isEmpty ? nil : fingerprint.timezone,
			locale: fingerprint.language,
			hideSpeechVoices: fingerprint.platform != .macOS
		)
	}

	// The JSON the engine reads, keys sorted so equal fingerprints compare
	// equal as text.
	public func json() throws -> String {
		let encoder = JSONEncoder()
		encoder.outputFormatting = [.sortedKeys, .withoutEscapingSlashes]

		return String(decoding: try encoder.encode(self), as: UTF8.self)
	}

	// The available area leaves room for the OS's own bars: the taskbar on
	// Windows, the menu bar on macOS, the panel on Linux. A full-screen
	// available area, or the host's menu bar inset, is a tell.
	static func screen(_ fingerprint: Fingerprint) -> Screen {
		let inset = switch fingerprint.platform {
		case .windows: 48
		case .macOS: 25
		case .linux: 27
		}

		return Screen(
			width: fingerprint.screen.width,
			height: fingerprint.screen.height,
			availWidth: fingerprint.screen.width,
			availHeight: fingerprint.screen.height - inset,
			availLeft: 0,
			availTop: fingerprint.platform == .macOS ? 25 : 0,
			colorDepth: fingerprint.screen.colorDepth
		)
	}

	// Chrome reports device memory as a power of two from 0.25 to 8 GB
	// (ApproximatedDeviceMemory); any other value is a value Chrome never
	// reports.
	static func approximatedDeviceMemory(_ gigabytes: Double) -> Double {
		guard gigabytes > 0.25 else {
			return 0.25
		}

		return min(8, pow(2, floor(log2(gigabytes))))
	}

	// The Electron app's seed (a Java-style string hash of the profile ID,
	// made positive), so a migrated profile keeps its canvas and audio
	// results.
	static func noiseSeed(_ profileID: String) -> UInt32 {
		var hash: Int32 = 0

		for unit in profileID.utf16 {
			hash = hash &* 31 &+ Int32(unit)
		}

		return UInt32(truncatingIfNeeded: abs(Int64(hash)))
	}
}
