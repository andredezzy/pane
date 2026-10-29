import Foundation

// The persisted profile model. Field names and raw values match the Electron
// app's profiles.json, so its file decodes as-is (Phase 5 migration) and the
// defaults below are the same ones its loader applies to older files.

public enum ProfileColor: String, Codable, CaseIterable, Sendable {
	case blue = "BLUE"
	case rose = "ROSE"
	case emerald = "EMERALD"
	case amber = "AMBER"
	case violet = "VIOLET"
	case orange = "ORANGE"
	case teal = "TEAL"
	case fuchsia = "FUCHSIA"
	case zinc = "ZINC"

	// Fixed-color swatches: they do not follow the theme (DESIGN.md).
	public var hex: String {
		switch self {
		case .blue: "#60a5fa"
		case .rose: "#fb7185"
		case .emerald: "#34d399"
		case .amber: "#fbbf24"
		case .violet: "#a78bfa"
		case .orange: "#f97316"
		case .teal: "#2dd4bf"
		case .fuchsia: "#e879f9"
		case .zinc: "#a1a1aa"
		}
	}
}

public enum Platform: String, Codable, CaseIterable, Sendable {
	case windows = "WINDOWS"
	case macOS = "MACOS"
	case linux = "LINUX"

	// As each spells its own name.
	public var title: String {
		switch self {
		case .windows: "Windows"
		case .macOS: "macOS"
		case .linux: "Linux"
		}
	}
}

public enum ProxyType: String, Codable, CaseIterable, Sendable {
	case http = "HTTP"
	case https = "HTTPS"
	case socks4 = "SOCKS4"
	case socks5 = "SOCKS5"
}

public struct Fingerprint: Codable, Hashable, Sendable {
	public struct Screen: Codable, Hashable, Sendable {
		public var width: Int
		public var height: Int
		public var colorDepth: Int

		public init(width: Int, height: Int, colorDepth: Int = 24) {
			self.width = width
			self.height = height
			self.colorDepth = colorDepth
		}

		public init(from decoder: Decoder) throws {
			let container = try decoder.container(keyedBy: CodingKeys.self)
			width = try container.decode(Int.self, forKey: .width)
			height = try container.decode(Int.self, forKey: .height)
			colorDepth = try container.decodeIfPresent(Int.self, forKey: .colorDepth) ?? 24
		}
	}

	public struct WebGL: Codable, Hashable, Sendable {
		public var vendor: String
		public var renderer: String

		public init(vendor: String, renderer: String) {
			self.vendor = vendor
			self.renderer = renderer
		}
	}

	public struct Noise: Codable, Hashable, Sendable {
		public var noise: Bool

		public init(noise: Bool) {
			self.noise = noise
		}
	}

	public var userAgent: String
	public var platform: Platform
	public var screen: Screen
	public var language: String
	public var languages: [String]
	public var timezone: String
	public var webgl: WebGL?
	public var hardwareConcurrency: Int
	public var deviceMemory: Double
	public var maxTouchPoints: Int
	public var canvas: Noise
	public var audio: Noise

	private enum CodingKeys: String, CodingKey {
		case userAgent, platform, screen, language, languages, timezone, webgl
		case hardwareConcurrency, deviceMemory, maxTouchPoints, canvas, audio
	}

	public init(
		userAgent: String,
		platform: Platform,
		screen: Screen,
		language: String,
		languages: [String],
		timezone: String,
		webgl: WebGL?,
		hardwareConcurrency: Int,
		deviceMemory: Double,
		maxTouchPoints: Int,
		canvas: Noise = Noise(noise: true),
		audio: Noise = Noise(noise: true)
	) {
		self.userAgent = userAgent
		self.platform = platform
		self.screen = screen
		self.language = language
		self.languages = languages
		self.timezone = timezone
		self.webgl = webgl
		self.hardwareConcurrency = hardwareConcurrency
		self.deviceMemory = deviceMemory
		self.maxTouchPoints = maxTouchPoints
		self.canvas = canvas
		self.audio = audio
	}

	public init(from decoder: Decoder) throws {
		let container = try decoder.container(keyedBy: CodingKeys.self)
		userAgent = try container.decode(String.self, forKey: .userAgent)
		platform = try container.decode(Platform.self, forKey: .platform)
		screen = try container.decode(Screen.self, forKey: .screen)
		language = try container.decode(String.self, forKey: .language)
		languages = try container.decode([String].self, forKey: .languages)
		timezone = try container.decode(String.self, forKey: .timezone)
		webgl = try container.decodeIfPresent(WebGL.self, forKey: .webgl)
		hardwareConcurrency = try container.decode(Int.self, forKey: .hardwareConcurrency)
		deviceMemory = try container.decode(Double.self, forKey: .deviceMemory)
		maxTouchPoints = try container.decode(Int.self, forKey: .maxTouchPoints)
		canvas = try container.decodeIfPresent(Noise.self, forKey: .canvas) ?? Noise(noise: true)
		audio = try container.decodeIfPresent(Noise.self, forKey: .audio) ?? Noise(noise: true)
	}

	public func encode(to encoder: Encoder) throws {
		var container = encoder.container(keyedBy: CodingKeys.self)
		try container.encode(userAgent, forKey: .userAgent)
		try container.encode(platform, forKey: .platform)
		try container.encode(screen, forKey: .screen)
		try container.encode(language, forKey: .language)
		try container.encode(languages, forKey: .languages)
		try container.encode(timezone, forKey: .timezone)
		try container.encode(webgl, forKey: .webgl)
		try container.encode(hardwareConcurrency, forKey: .hardwareConcurrency)
		try container.encode(deviceMemory, forKey: .deviceMemory)
		try container.encode(maxTouchPoints, forKey: .maxTouchPoints)
		try container.encode(canvas, forKey: .canvas)
		try container.encode(audio, forKey: .audio)
	}
}

public struct ProxyConfig: Codable, Hashable, Sendable {
	public var proxyType: ProxyType
	public var host: String
	public var port: Int
	public var username: String?
	public var password: String?

	public init(proxyType: ProxyType, host: String, port: Int, username: String? = nil, password: String? = nil) {
		self.proxyType = proxyType
		self.host = host
		self.port = port
		self.username = username
		self.password = password
	}

	public func encode(to encoder: Encoder) throws {
		var container = encoder.container(keyedBy: CodingKeys.self)
		try container.encode(proxyType, forKey: .proxyType)
		try container.encode(host, forKey: .host)
		try container.encode(port, forKey: .port)
		try container.encode(username, forKey: .username)
		try container.encode(password, forKey: .password)
	}
}

public struct Tab: Codable, Hashable, Identifiable, Sendable {
	public var id: String
	public var url: String
	public var title: String
	// A data: URL, or empty while the page has none.
	public var favicon: String

	public init(id: String, url: String, title: String = "", favicon: String = "") {
		self.id = id
		self.url = url
		self.title = title
		self.favicon = favicon
	}

	public init(from decoder: Decoder) throws {
		let container = try decoder.container(keyedBy: CodingKeys.self)
		id = try container.decode(String.self, forKey: .id)
		url = try container.decode(String.self, forKey: .url)
		title = try container.decodeIfPresent(String.self, forKey: .title) ?? ""
		favicon = try container.decodeIfPresent(String.self, forKey: .favicon) ?? ""
	}
}

public struct BrowserProfile: Codable, Hashable, Identifiable, Sendable {
	public var id: String
	public var name: String
	public var color: ProfileColor
	public var group: String?
	// nil = no spoofing: the profile presents the real Chrome surface.
	public var fingerprint: Fingerprint?
	public var proxy: ProxyConfig?
	// Exempts the profile from automatic sleep and unload (CONTEXT.md).
	public var keepLoaded: Bool
	public var tabs: [Tab]
	public var createdAt: String
	public var updatedAt: String

	private enum CodingKeys: String, CodingKey {
		case id, name, color, group, fingerprint, proxy, keepLoaded, tabs, createdAt, updatedAt
	}

	public init(
		id: String,
		name: String,
		color: ProfileColor = .blue,
		group: String? = nil,
		fingerprint: Fingerprint? = nil,
		proxy: ProxyConfig? = nil,
		keepLoaded: Bool = false,
		tabs: [Tab] = [],
		createdAt: String,
		updatedAt: String
	) {
		self.id = id
		self.name = name
		self.color = color
		self.group = group
		self.fingerprint = fingerprint
		self.proxy = proxy
		self.keepLoaded = keepLoaded
		self.tabs = tabs
		self.createdAt = createdAt
		self.updatedAt = updatedAt
	}

	public init(from decoder: Decoder) throws {
		let container = try decoder.container(keyedBy: CodingKeys.self)
		id = try container.decode(String.self, forKey: .id)
		name = try container.decode(String.self, forKey: .name)
		color = try container.decodeIfPresent(ProfileColor.self, forKey: .color) ?? .blue
		group = try container.decodeIfPresent(String.self, forKey: .group)
		fingerprint = try container.decodeIfPresent(Fingerprint.self, forKey: .fingerprint)
		proxy = try container.decodeIfPresent(ProxyConfig.self, forKey: .proxy)
		keepLoaded = try container.decodeIfPresent(Bool.self, forKey: .keepLoaded) ?? false
		tabs = try container.decode([Tab].self, forKey: .tabs)
		createdAt = try container.decode(String.self, forKey: .createdAt)
		updatedAt = try container.decode(String.self, forKey: .updatedAt)
	}

	// Explicit nulls, as the Electron app writes them.
	public func encode(to encoder: Encoder) throws {
		var container = encoder.container(keyedBy: CodingKeys.self)
		try container.encode(id, forKey: .id)
		try container.encode(name, forKey: .name)
		try container.encode(color, forKey: .color)
		try container.encode(group, forKey: .group)
		try container.encode(fingerprint, forKey: .fingerprint)
		try container.encode(proxy, forKey: .proxy)
		try container.encode(keepLoaded, forKey: .keepLoaded)
		try container.encode(tabs, forKey: .tabs)
		try container.encode(createdAt, forKey: .createdAt)
		try container.encode(updatedAt, forKey: .updatedAt)
	}
}
