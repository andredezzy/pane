// How a profile's proxy reaches Chrome (apps/veil profile/proxy-relay.ts and
// profile-tabs.ts): Chrome's proxy rule for it, whether it needs the app's
// SOCKS relay, and the WebRTC policy that keeps the real address from leaking
// past it.
extension ProxyConfig {
	// Chromium cannot authenticate to a SOCKS proxy, so a SOCKS proxy with
	// credentials is reached through the app's local relay, which signs in
	// upstream.
	public var needsRelay: Bool {
		(proxyType == .socks4 || proxyType == .socks5) && !(username ?? "").isEmpty
	}

	// Chrome's fixed proxy rule: the proxy itself, or the local relay on
	// `relayPort` when it needs one.
	public func rules(relayPort: Int?) -> String {
		if needsRelay, let relayPort {
			return "socks5://127.0.0.1:\(relayPort)"
		}

		return "\(proxyType.rawValue.lowercased())://\(host):\(port)"
	}

	// Whether Chrome's authentication challenge comes from this proxy: Chrome
	// names it "scheme://host:port" ("http://127.0.0.1:3128").
	public func isChallenger(_ challenger: String) -> Bool {
		let hostPort = challenger.range(of: "://").map { String(challenger[$0.upperBound...]) } ?? challenger

		return hostPort.lowercased() == "\(host):\(port)".lowercased()
	}
}

public enum WebRTCPolicy {
	// WebRTC's ICE candidates would leak the real LAN address past a proxy
	// (its rules govern TCP only): private interfaces stay hidden, and a
	// proxied profile sends all UDP through the proxy, which means none.
	public static func forProfile(proxied: Bool) -> String {
		proxied ? "disable_non_proxied_udp" : "default_public_interface_only"
	}
}
