import Foundation

// A fingerprint's UA Client Hints (apps/veil profile/client-hints.ts): one
// derivation feeds both navigator.userAgentData and the Sec-CH-UA-* headers,
// so the two surfaces never disagree.

public struct UABrand: Codable, Hashable, Sendable {
	public var brand: String
	public var version: String

	public init(brand: String, version: String) {
		self.brand = brand
		self.version = version
	}
}

public struct ClientHints: Codable, Hashable, Sendable {
	public var platform: String
	public var platformVersion: String
	public var architecture: String
	public var bitness: String
	public var mobile: Bool
	public var brands: [UABrand]
	public var fullVersionList: [UABrand]
	public var uaFullVersion: String
}

// Rewrites the Chrome version a stored User-Agent claims to the engine's, so a
// fingerprint never advertises a Chrome build the engine is not: a stale claim
// is a contradiction Cloudflare Turnstile detects by probing the runtime. Any
// version form is replaced ("136.0.0.0" or a bare "136").
public func reconcileChromeVersion(_ claimedUA: String, realUA: String) -> String {
	guard let realVersion = realUA.firstMatch(of: /Chrome\/(\d+\.\d+\.\d+\.\d+)/)?.1 else {
		return claimedUA
	}

	return claimedUA.replacing(/Chrome\/[\d.]+/, maxReplacements: 1) { _ in "Chrome/\(realVersion)" }
}

extension Fingerprint {
	// navigator.platform for the claimed OS.
	public var navigatorPlatform: String {
		switch platform {
		case .windows: "Win32"
		case .macOS: "MacIntel"
		case .linux: "Linux x86_64"
		}
	}

	// The fingerprint's Client Hints, as Chrome of the version its User-Agent
	// claims would send them. `engineVersion` is the engine's full version
	// ("156.0.8078.48"): Chrome reports it in the full version list, while its
	// User-Agent only carries the major ("Chrome/156.0.0.0").
	public func clientHints(engineVersion: String) -> ClientHints {
		let majorText = userAgent.firstMatch(of: /Chrome\/(\d+)/).map { String($0.1) } ?? "136"
		let major = Int(majorText) ?? 136
		let fullVersion: String

		if engineVersion.split(separator: ".").first.map(String.init) == majorText {
			fullVersion = engineVersion
		} else {
			fullVersion = userAgent.firstMatch(of: /Chrome\/(\d+\.\d+\.\d+\.\d+)/).map { String($0.1) } ?? "\(majorText).0.0.0"
		}

		return ClientHints(
			platform: uaPlatform,
			platformVersion: platformVersion,
			architecture: isAppleSilicon ? "arm" : "x86",
			bitness: "64",
			mobile: false,
			brands: brandList(seed: major, brand: "Google Chrome", version: majorText, fullVersions: false),
			fullVersionList: brandList(seed: major, brand: "Google Chrome", version: fullVersion, fullVersions: true),
			uaFullVersion: fullVersion
		)
	}

	private var uaPlatform: String {
		switch platform {
		case .windows: "Windows"
		case .macOS: "macOS"
		case .linux: "Linux"
		}
	}

	// Chrome reports an empty platform version on Linux (user_agent_utils.cc
	// returns an empty string there); any other value paired with "Linux" is a
	// tell. Windows and macOS report their OS version.
	private var platformVersion: String {
		switch platform {
		case .windows: "15.0.0"
		case .macOS: "14.6.1"
		case .linux: ""
		}
	}

	private var isAppleSilicon: Bool {
		(webgl?.renderer ?? "").contains(/(?i)apple|\bm[1-4]\b|arm/)
	}
}

// The Sec-CH-UA-* header values (lowercase names) the hints serialize to, as
// Chrome writes them.
public func clientHintHeaders(_ hints: ClientHints) -> [String: String] {
	func serialize(_ list: [UABrand]) -> String {
		list.map { "\"\($0.brand)\";v=\"\($0.version)\"" }.joined(separator: ", ")
	}

	return [
		"sec-ch-ua": serialize(hints.brands),
		"sec-ch-ua-full-version-list": serialize(hints.fullVersionList),
		"sec-ch-ua-full-version": "\"\(hints.uaFullVersion)\"",
		"sec-ch-ua-platform": "\"\(hints.platform)\"",
		"sec-ch-ua-platform-version": "\"\(hints.platformVersion)\"",
		"sec-ch-ua-arch": "\"\(hints.architecture)\"",
		"sec-ch-ua-bitness": "\"\(hints.bitness)\"",
		"sec-ch-ua-wow64": "?0",
		"sec-ch-ua-model": "\"\"",
		"sec-ch-ua-form-factors": "\"Desktop\"",
		"sec-ch-ua-mobile": hints.mobile ? "?1" : "?0",
	]
}

// Chromium's brand list (GenerateBrandVersionList in user_agent_utils.cc): a
// GREASE brand, "Chromium", and the product brand, in a permutation seeded by
// the major version. Reproducing it keeps the list plausible for any version.
func brandList(seed: Int, brand: String?, version: String, fullVersions: Bool) -> [UABrand] {
	let greaseChars = [" ", "(", ":", "-", ".", "/", ")", ";", "=", "?", "_"]
	let greaseVersions = ["8", "99", "24"]
	let greaseVersion = greaseVersions[seed % greaseVersions.count]

	var list = [
		UABrand(
			brand: "Not\(greaseChars[seed % greaseChars.count])A\(greaseChars[(seed + 1) % greaseChars.count])Brand",
			version: fullVersions ? "\(greaseVersion).0.0.0" : greaseVersion
		),
		UABrand(brand: "Chromium", version: version),
	]

	if let brand {
		list.append(UABrand(brand: brand, version: version))
	}

	let order = brandOrder(seed: seed, count: list.count)
	var shuffled = list

	for (index, position) in order.enumerated() {
		shuffled[position] = list[index]
	}

	return shuffled
}

// GetRandomOrder in user_agent_utils.cc, for two or three brands.
private func brandOrder(seed: Int, count: Int) -> [Int] {
	if count == 2 {
		return [seed % 2, (seed + 1) % 2]
	}

	let orders = [[0, 1, 2], [0, 2, 1], [1, 0, 2], [1, 2, 0], [2, 0, 1], [2, 1, 0]]

	return orders[seed % orders.count]
}
