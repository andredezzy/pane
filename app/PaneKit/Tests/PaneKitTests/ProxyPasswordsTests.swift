import Foundation
import PaneKit
import Testing

@Suite("ProxyPasswords")
struct ProxyPasswordsTests {
	private func profile(_ id: String, username: String? = nil, password: String? = nil, proxied: Bool = true) -> BrowserProfile {
		BrowserProfile(
			id: id,
			name: id,
			proxy: proxied ? ProxyConfig(proxyType: .http, host: "proxy.example", port: 3128, username: username, password: password) : nil,
			createdAt: "2026-10-27T00:00:00.000Z",
			updatedAt: "2026-10-27T00:00:00.000Z"
		)
	}

	@Test("the passwords an earlier Pane kept apart go back into its profiles")
	func filled() {
		let plain = [profile("a", username: "u"), profile("b", username: "u"), profile("c", proxied: false)]
		let profiles = ProxyPasswords.filled(plain, from: ["a": "s3cret", "c": "no proxy"])

		#expect(profiles.map(\.proxy?.password) == ["s3cret", nil, nil])
	}

	@Test("a password the file still holds is read, and wins")
	func legacy() {
		let legacy = [profile("a", username: "u", password: "from-file")]

		#expect(ProxyPasswords.needsStore(legacy))
		#expect(ProxyPasswords.filled(legacy, from: ["a": "stale"]).first?.proxy?.password == "from-file")
	}

	@Test("only a proxy that signs in needs the store")
	func needsStore() {
		#expect(!ProxyPasswords.needsStore([profile("a"), profile("b", proxied: false)]))
		#expect(ProxyPasswords.needsStore([profile("a", username: "u")]))
	}
}
