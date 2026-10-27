import Foundation

// The profile sheet's rules (apps/veil profile-sheet.tsx and
// default-fingerprints.ts): the fingerprint a platform gets, the proxy string
// shortcut, and when an edit keeps the stored fingerprint.

extension Fingerprint {
	// default-fingerprints.ts: one per platform. The engine replaces the
	// claimed Chrome version with its own (5.2).
	public static func preset(_ platform: Platform) -> Fingerprint {
		switch platform {
		case .windows:
			Fingerprint(
				userAgent: "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/136.0.0.0 Safari/537.36",
				platform: .windows,
				screen: Screen(width: 1920, height: 1080, colorDepth: 24),
				language: "en-US",
				languages: ["en-US", "en"],
				timezone: "America/New_York",
				webgl: WebGL(vendor: "Google Inc. (NVIDIA)", renderer: "ANGLE (NVIDIA, NVIDIA GeForce RTX 3060 Direct3D11 vs_5_0 ps_5_0, D3D11)"),
				hardwareConcurrency: 8,
				deviceMemory: 8,
				maxTouchPoints: 0
			)
		case .macOS:
			Fingerprint(
				userAgent: "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/136.0.0.0 Safari/537.36",
				platform: .macOS,
				screen: Screen(width: 1440, height: 900, colorDepth: 30),
				language: "en-US",
				languages: ["en-US", "en"],
				timezone: "America/New_York",
				webgl: WebGL(vendor: "Google Inc. (Apple)", renderer: "ANGLE (Apple, Apple M1, OpenGL 4.1)"),
				hardwareConcurrency: 8,
				deviceMemory: 8,
				maxTouchPoints: 0
			)
		case .linux:
			Fingerprint(
				userAgent: "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/136.0.0.0 Safari/537.36",
				platform: .linux,
				screen: Screen(width: 1920, height: 1080, colorDepth: 24),
				language: "en-US",
				languages: ["en-US", "en"],
				timezone: "America/New_York",
				webgl: WebGL(vendor: "Google Inc. (Intel)", renderer: "ANGLE (Intel, Mesa Intel(R) UHD Graphics 630, OpenGL 4.6)"),
				hardwareConcurrency: 4,
				deviceMemory: 8,
				maxTouchPoints: 0
			)
		}
	}

	// The fingerprint a new profile on `platform` gets. One on the host's own
	// platform claims the host's real GPU, so its WebGL claim matches what
	// the hardware draws (Cloudflare Turnstile flags a spoofed renderer); a
	// cross-OS one keeps the preset's, which no claim can make consistent.
	public static func generated(for platform: Platform, host: Platform = .macOS, hostWebGL: WebGL?) -> Fingerprint {
		var fingerprint = preset(platform)

		if platform == host, let hostWebGL {
			fingerprint.webgl = hostWebGL
		}

		return fingerprint
	}

	// What an edit saves: the stored fingerprint, unless the user changed
	// what generates it (spoofing on or off, or the platform). Regenerating
	// on a rename would re-pin the GPU to the current host and pick up preset
	// changes, changing what sites see of the profile.
	public static func edited(stored: Fingerprint?, spoofing: Bool, platform: Platform, generate: () -> Fingerprint) -> Fingerprint? {
		guard spoofing else {
			return nil
		}

		if let stored, stored.platform == platform {
			return stored
		}

		return generate()
	}
}

// The proxy field's shortcut: "host:port", "host:port:user", or
// "host:port:user:password", as proxy sellers list them.
public struct ProxyString: Equatable, Sendable {
	public var host: String
	public var port: Int
	public var username: String?
	public var password: String?

	public init?(_ raw: String) {
		let parts = raw.trimmingCharacters(in: .whitespaces).split(separator: ":", omittingEmptySubsequences: false).map(String.init)

		guard parts.count >= 2, let host = parts.first, !host.isEmpty, let port = Int(parts[1]) else {
			return nil
		}

		self.host = host
		self.port = port
		username = parts.count > 2 && !parts[2].isEmpty ? parts[2] : nil
		password = parts.count > 3 && !parts[3].isEmpty ? parts[3] : nil
	}
}
