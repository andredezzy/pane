import PaneKit
import Testing

@Suite("ProxyRoute")
struct ProxyRouteTests {
	@Test("an IPv6 host takes brackets for Chrome, typed with them or not, and none for a connection")
	func ipv6Host() {
		let bare = ProxyConfig(proxyType: .socks5, host: "2606:4700:4700::1111", port: 1080)
		let bracketed = ProxyConfig(proxyType: .http, host: "[2606:4700:4700::1111]", port: 3128)
		let named = ProxyConfig(proxyType: .http, host: "proxy.example", port: 3128)

		#expect(bare.rules(relayPort: nil) == "socks5://[2606:4700:4700::1111]:1080")
		#expect(bracketed.rules(relayPort: nil) == "http://[2606:4700:4700::1111]:3128")
		#expect(bare.isChallenger("socks5://[2606:4700:4700::1111]:1080"))
		#expect(bracketed.isChallenger("http://[2606:4700:4700::1111]:3128"))

		#expect(bare.connectionHost == "2606:4700:4700::1111")
		#expect(bracketed.connectionHost == "2606:4700:4700::1111")
		#expect(named.connectionHost == "proxy.example")
	}

	@Test("HTTP and HTTPS proxies go to Chrome as they are, credentials and all")
	func httpProxies() {
		let http = ProxyConfig(proxyType: .http, host: "proxy.example", port: 8080, username: "u", password: "p")
		let https = ProxyConfig(proxyType: .https, host: "proxy.example", port: 443)

		#expect(!http.needsRelay)
		#expect(http.rules(relayPort: nil) == "http://proxy.example:8080")
		#expect(https.rules(relayPort: nil) == "https://proxy.example:443")
	}

	@Test("SOCKS without credentials goes straight to Chrome")
	func socksWithoutCredentials() {
		let socks5 = ProxyConfig(proxyType: .socks5, host: "10.0.0.2", port: 1080)
		let socks4 = ProxyConfig(proxyType: .socks4, host: "10.0.0.2", port: 1080, username: "")

		#expect(!socks5.needsRelay)
		#expect(!socks4.needsRelay)
		#expect(socks5.rules(relayPort: 5555) == "socks5://10.0.0.2:1080")
		#expect(socks4.rules(relayPort: nil) == "socks4://10.0.0.2:1080")
	}

	@Test("SOCKS with credentials goes through the local relay")
	func socksWithCredentials() {
		let socks5 = ProxyConfig(proxyType: .socks5, host: "10.0.0.2", port: 1080, username: "u", password: "p")
		let socks4 = ProxyConfig(proxyType: .socks4, host: "10.0.0.2", port: 1080, username: "u")

		#expect(socks5.needsRelay)
		#expect(socks4.needsRelay)
		#expect(socks5.rules(relayPort: 54321) == "socks5://127.0.0.1:54321")
		#expect(socks4.rules(relayPort: 54321) == "socks5://127.0.0.1:54321")
	}

	@Test("a challenge is the proxy's when it names its host and port")
	func challenger() {
		let proxy = ProxyConfig(proxyType: .http, host: "Proxy.example", port: 3128)

		#expect(proxy.isChallenger("http://proxy.example:3128"))
		#expect(proxy.isChallenger("proxy.example:3128"))
		#expect(!proxy.isChallenger("http://proxy.example:8080"))
		#expect(!proxy.isChallenger("https://other.example:3128"))
	}

	@Test("a challenge is matched in the form Chrome names it, whatever was typed")
	func challengerForms() {
		let ipv6 = ProxyConfig(proxyType: .http, host: "2001:0DB8:0:0::0001", port: 3128)
		#expect(ipv6.isChallenger("http://[2001:db8::1]:3128"))
		#expect(!ipv6.isChallenger("http://[2001:db8::2]:3128"))
		#expect(!ipv6.isChallenger("http://[2001:db8::1]:8080"))

		let bracketed = ProxyConfig(proxyType: .http, host: "[0:0:0:0:0:0:0:1]", port: 3128)
		#expect(bracketed.isChallenger("http://[::1]:3128"))

		// Chrome leaves out the scheme's own port.
		#expect(ProxyConfig(proxyType: .http, host: "proxy.example", port: 80).isChallenger("http://proxy.example"))
		#expect(ProxyConfig(proxyType: .https, host: "proxy.example", port: 443).isChallenger("https://proxy.example"))
		#expect(!ProxyConfig(proxyType: .http, host: "proxy.example", port: 443).isChallenger("http://proxy.example"))

		// A name in punycode.
		#expect(ProxyConfig(proxyType: .http, host: "ex\u{E4}mple.test", port: 3128).isChallenger("http://xn--exmple-cua.test:3128"))
	}

	@Test("a proxied profile keeps all UDP to the proxy; others hide private interfaces")
	func webRTC() {
		#expect(WebRTCPolicy.forProfile(proxied: true) == "disable_non_proxied_udp")
		#expect(WebRTCPolicy.forProfile(proxied: false) == "default_public_interface_only")
	}
}
