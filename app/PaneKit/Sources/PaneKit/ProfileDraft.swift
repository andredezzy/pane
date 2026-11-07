import Foundation

// The profile sheet's form (apps/veil profile-sheet.tsx), as the user fills
// it in: what it starts from, what keeps it from saving, and what it saves.
// The sheet holds one; the controller applies it.
public struct ProfileDraft: Equatable, Sendable {
	// Pane runs on macOS only: the host's platform.
	public static let hostPlatform = Platform.macOS

	// What keeps it from saving, the first found, shown as a red ring around its
	// field, whose words are for VoiceOver.
	public enum Problem: Equatable, Sendable {
		case nameMissing
		case hostMissing
		case hostInvalid
		case portInvalid

		public var message: String {
			switch self {
			case .nameMissing: "Profile name is required"
			case .hostMissing: "Proxy host is required"
			case .hostInvalid: "Host must be a name or an IP address"
			case .portInvalid: Self.portMessage
			}
		}

		public static let portMessage = "Port must be between 1 and 65535"
	}

	public var name = ""
	public var color = ProfileColor.blue
	public var group = ""
	public var savesHistory = true
	public var spoofing = true
	public var platform = hostPlatform
	// The zone the fingerprint claims; empty, the computer's.
	public var timeZone = ""
	public var proxyEnabled = false
	public var proxyType = ProxyType.http
	public var proxyHost = ""
	public var proxyPort = "8080"
	public var proxyUsername = ""
	public var proxyPassword = ""

	// A new profile's form.
	public init() {}

	// An existing profile's, as it is.
	public init(profile: BrowserProfile) {
		name = profile.name
		color = profile.color
		group = profile.group ?? ""
		savesHistory = profile.savesHistory
		spoofing = profile.fingerprint != nil
		platform = profile.fingerprint?.platform ?? Self.hostPlatform
		timeZone = profile.fingerprint?.timezone ?? ""
		proxyEnabled = profile.proxy != nil

		if let proxy = profile.proxy {
			proxyType = proxy.proxyType
			proxyHost = proxy.host
			proxyPort = String(proxy.port)
			proxyUsername = proxy.username ?? ""
			proxyPassword = proxy.password ?? ""
		}
	}

	public var trimmedName: String {
		name.trimmingCharacters(in: .whitespaces)
	}

	private var trimmedHost: String {
		proxyHost.trimmingCharacters(in: .whitespaces)
	}

	// The sheet says "Its traffic goes through this proxy" while the switch is on:
	// no host, or no valid port, would save as no proxy, and the profile would go
	// direct, and a host Chrome cannot read would fail its every page. None of
	// them saves.
	public func validate() -> Problem? {
		if trimmedName.isEmpty {
			return .nameMissing
		}

		guard proxyEnabled else {
			return nil
		}

		return hostProblem ?? (ProxyConfig.port(proxyPort) == nil ? .portInvalid : nil)
	}

	public var hostProblem: Problem? {
		if trimmedHost.isEmpty {
			return .hostMissing
		}

		return ProxyConfig.isHost(trimmedHost) ? nil : .hostInvalid
	}

	// What of `problem` an edit leaves: its ring stays until its field can save, so
	// "http:" on the way to something else is still no host, and "700" on the way
	// to 7000 goes at once. The proxy's go with its switch.
	public func remaining(_ problem: Problem) -> Problem? {
		switch problem {
		case .nameMissing: trimmedName.isEmpty ? .nameMissing : nil
		case .hostMissing, .hostInvalid: proxyEnabled ? hostProblem : nil
		case .portInvalid: proxyEnabled && ProxyConfig.port(proxyPort) == nil ? .portInvalid : nil
		}
	}

	// The proxy as filled in, once it has a host and a valid port.
	public var proxy: ProxyConfig? {
		guard proxyEnabled, !trimmedHost.isEmpty, let port = ProxyConfig.port(proxyPort) else {
			return nil
		}

		return ProxyConfig(
			proxyType: proxyType,
			host: trimmedHost,
			port: port,
			username: proxyUsername.isEmpty ? nil : proxyUsername,
			password: proxyPassword.isEmpty ? nil : proxyPassword
		)
	}

	// The paste shortcut (ProxyString) fills the proxy's fields; a
	// credential it leaves out keeps what was typed. Whether it parsed.
	@discardableResult
	public mutating func pasteProxy(_ raw: String) -> Bool {
		guard let parsed = ProxyString(raw) else {
			return false
		}

		proxyHost = parsed.host
		proxyPort = String(parsed.port)

		if let username = parsed.username {
			proxyUsername = username
		}

		if let password = parsed.password {
			proxyPassword = password
		}

		return true
	}

	// What saving writes into the profile. A fingerprint is generated only
	// when spoofing turns on or the platform changes (Fingerprint.edited).
	public func apply(to profile: inout BrowserProfile, generate: () -> Fingerprint) {
		let group = group.trimmingCharacters(in: .whitespaces)

		profile.name = trimmedName
		profile.color = color
		profile.group = group.isEmpty ? nil : group
		profile.savesHistory = savesHistory
		profile.fingerprint = Fingerprint.edited(stored: profile.fingerprint, spoofing: spoofing, platform: platform, generate: generate)
		profile.fingerprint?.timezone = timeZone
		profile.proxy = proxy
	}
}
