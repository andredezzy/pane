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

	@Test("profiles.json keeps no password, and reads one back from the store")
	func roundTrip() throws {
		let profiles = [profile("a", username: "u", password: "s3cret"), profile("b", username: "u"), profile("c", proxied: false)]
		let stored = ProxyPasswords.of(profiles)

		#expect(stored == ["a": "s3cret"])

		let file = ProfilesState(profiles: ProxyPasswords.stripped(profiles))
		let data = try JSONEncoder().encode(file)
		#expect(!String(decoding: data, as: UTF8.self).contains("s3cret"))

		let read = try JSONDecoder().decode(ProfilesState.self, from: data).profiles
		#expect(ProxyPasswords.filled(read, from: stored) == profiles)
	}

	@Test("a password the file still holds is read, and wins until moved")
	func legacy() {
		let legacy = [profile("a", username: "u", password: "from-file")]

		#expect(ProxyPasswords.needsStore(legacy))
		#expect(ProxyPasswords.filled(legacy, from: ["a": "stale"]).first?.proxy?.password == "from-file")
		#expect(ProxyPasswords.of(legacy) == ["a": "from-file"])
	}

	@Test("only a proxy that signs in needs the store")
	func needsStore() {
		#expect(!ProxyPasswords.needsStore([profile("a"), profile("b", proxied: false)]))
		#expect(ProxyPasswords.needsStore([profile("a", username: "u")]))
	}
}
