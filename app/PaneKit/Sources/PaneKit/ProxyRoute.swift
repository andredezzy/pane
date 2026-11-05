import Foundation

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

		return "\(proxyType.rawValue.lowercased())://\(chromeHost):\(port)"
	}

	// Whether Chrome's authentication challenge comes from this proxy: Chrome
	// names it "scheme://host:port" in its own form, whatever was typed: "[::1]"
	// for "0:0:0:0:0:0:0:1", a name in lowercase and punycode, and no port where
	// it is the scheme's. Missed, the user is asked for what the profile holds.
	public func isChallenger(_ challenger: String) -> Bool {
		guard
			let named = URLComponents(string: challenger.contains("://") ? challenger : "http://\(challenger)"),
			let namedHost = named.encodedHost
		else {
			return false
		}

		return (named.port ?? (named.scheme == "https" ? 443 : 80)) == port
			&& Self.canonicalHost(namedHost) == Self.canonicalHost(connectionHost)
	}

	// An IPv6 host, as typed with its brackets or without. Chrome cannot read a
	// bare one's colons, and fails every request of the profile.
	private var chromeHost: String {
		host.contains(":") && !host.hasPrefix("[") ? "[\(host)]" : host
	}

	private static func canonicalHost(_ host: String) -> String {
		let bare = host.hasPrefix("[") && host.hasSuffix("]") ? String(host.dropFirst().dropLast()) : host
		var address = in6_addr()

		if inet_pton(AF_INET6, bare, &address) == 1 {
			var text = [CChar](repeating: 0, count: Int(INET6_ADDRSTRLEN))

			if inet_ntop(AF_INET6, &address, &text, socklen_t(INET6_ADDRSTRLEN)) != nil {
				return String(decoding: text.prefix { $0 != 0 }.map(UInt8.init(bitPattern:)), as: UTF8.self)
			}
		}

		return (URLComponents(string: "http://\(bare)")?.encodedHost ?? bare).lowercased()
	}

	// The host for a connection of Pane's own: NWEndpoint reads brackets as part
	// of a name.
	public var connectionHost: String {
		host.hasPrefix("[") && host.hasSuffix("]") ? String(host.dropFirst().dropLast()) : host
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
