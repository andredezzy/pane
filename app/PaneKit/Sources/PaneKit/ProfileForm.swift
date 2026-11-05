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
				timezone: "",
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
				timezone: "",
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
				timezone: "",
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
// "host:port:user:password", as proxy sellers list them. The password is the
// rest of the line, colons and all. An IPv6 host takes its brackets, and an
// IPv6 address alone is a host, not a shortcut: the sheet reads what is in the
// host field as one.
public struct ProxyString: Equatable, Sendable {
	public var host: String
	public var port: Int
	public var username: String?
	public var password: String?

	public init?(_ raw: String) {
		let line = raw.trimmingCharacters(in: .whitespaces)
		var address = in6_addr()

		guard inet_pton(AF_INET6, line, &address) != 1 else {
			return nil
		}

		let hostEnd = line.hasPrefix("[")
			? line.firstIndex(of: "]").map(line.index(after:))
			: line.firstIndex(of: ":")

		guard let hostEnd, line[hostEnd...].first == ":" else {
			return nil
		}

		let host = String(line[..<hostEnd])
		let fields = line[hostEnd...].dropFirst().split(separator: ":", omittingEmptySubsequences: false).map(String.init)

		guard !host.isEmpty, let port = ProxyConfig.port(fields[0]) else {
			return nil
		}

		let password = fields.dropFirst(2).joined(separator: ":")

		self.host = host
		self.port = port
		username = fields.count > 1 && !fields[1].isEmpty ? fields[1] : nil
		self.password = password.isEmpty ? nil : password
	}
}

extension ProxyConfig {
	// Whether Chrome can read this as a proxy's host: a name, an IPv4 address, or
	// an IPv6 one with its brackets or without. A scheme, credentials, a path, or
	// spaces make a rule Chrome drops, which fails the profile's every request.
	public static func isHost(_ text: String) -> Bool {
		let bare = text.hasPrefix("[") && text.hasSuffix("]") ? String(text.dropFirst().dropLast()) : text
		var ipv6 = in6_addr()

		// inet_pton takes a zone ("fe80::1%en0"), and Chrome does not.
		if inet_pton(AF_INET6, bare, &ipv6) == 1 {
			return !bare.contains("%")
		}

		guard bare == text, !text.isEmpty, !text.hasPrefix("."), !text.contains("..") else {
			return false
		}

		if text.allSatisfy({ $0 == "." || $0.isNumber }) {
			var ipv4 = in_addr()

			return inet_pton(AF_INET, text, &ipv4) == 1
		}

		return text.unicodeScalars.allSatisfy { scalar in
			scalar.isASCII
				? CharacterSet.alphanumerics.contains(scalar) || "-._".unicodeScalars.contains(scalar)
				: !scalar.properties.isWhitespace
		}
	}

	// The ports a proxy can listen on: TCP's, without 0.
	public static let ports = 1 ... 65535

	// The port typed or pasted, or nil when it is not one of `ports`: the
	// SOCKS relay would trap on it, and Chrome would fail every request of the
	// profile.
	public static func port(_ text: String) -> Int? {
		Int(text.trimmingCharacters(in: .whitespaces)).flatMap { ports.contains($0) ? $0 : nil }
	}
}
