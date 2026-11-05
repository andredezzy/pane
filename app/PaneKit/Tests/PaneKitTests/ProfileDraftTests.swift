import PaneKit
import Testing

private func profile(fingerprint: Fingerprint? = nil, proxy: ProxyConfig? = nil) -> BrowserProfile {
	BrowserProfile(id: "p", name: "Work", color: .emerald, group: "Clients", fingerprint: fingerprint, proxy: proxy, createdAt: "", updatedAt: "")
}

@Suite("profile draft")
struct ProfileDraftTests {
	@Test("a new profile's form: spoofing on the host's platform, no proxy")
	func newForm() {
		let draft = ProfileDraft()

		#expect(draft.name.isEmpty)
		#expect(draft.savesHistory)
		#expect(draft.spoofing)
		#expect(draft.platform == ProfileDraft.hostPlatform)
		#expect(!draft.proxyEnabled)
		#expect(draft.proxyPort == "8080")
	}

	@Test("saving an edit untouched leaves the profile as it was")
	func untouchedEdit() {
		let proxy = ProxyConfig(proxyType: .socks5, host: "proxy.example", port: 1080, username: "user", password: "p:ss")
		let original = profile(fingerprint: .preset(.windows), proxy: proxy)
		var saved = original

		ProfileDraft(profile: original).apply(to: &saved) {
			Issue.record("regenerated the fingerprint")
			return .preset(.macOS)
		}

		#expect(saved == original)
	}

	@Test("an existing profile fills the form")
	func fillsForm() {
		let draft = ProfileDraft(profile: profile(fingerprint: .preset(.linux), proxy: ProxyConfig(proxyType: .https, host: "h", port: 3128)))

		#expect(draft.name == "Work")
		#expect(draft.color == .emerald)
		#expect(draft.group == "Clients")
		#expect(draft.spoofing)
		#expect(draft.platform == .linux)
		#expect(draft.proxyEnabled)
		#expect(draft.proxyType == .https)
		#expect(draft.proxyPort == "3128")
		#expect(draft.proxyUsername.isEmpty)
		#expect(!ProfileDraft(profile: profile()).spoofing)
	}

	@Test("the time zone goes from the profile to the form and back, and survives another platform")
	func timeZone() {
		var claimed = Fingerprint.preset(.macOS)
		claimed.timezone = "Asia/Tokyo"
		var saved = profile(fingerprint: claimed)

		var draft = ProfileDraft(profile: saved)
		#expect(draft.timeZone == "Asia/Tokyo")

		draft.platform = .windows
		draft.apply(to: &saved) { .preset(.windows) }
		#expect(saved.fingerprint?.platform == .windows)
		#expect(saved.fingerprint?.timezone == "Asia/Tokyo")

		draft.timeZone = ""
		draft.apply(to: &saved) { .preset(.windows) }
		#expect(saved.fingerprint?.timezone == "")

		draft.timeZone = "Asia/Tokyo"
		draft.spoofing = false
		draft.apply(to: &saved) { .preset(.windows) }
		#expect(saved.fingerprint == nil)
		#expect(ProfileDraft(profile: saved).timeZone.isEmpty)
	}

	@Test("a new profile claims no time zone: it follows the computer")
	func newTimeZone() {
		#expect(ProfileDraft().timeZone.isEmpty)

		for platform in Platform.allCases {
			#expect(Fingerprint.preset(platform).timezone.isEmpty)
		}
	}

	@Test("turning history off owes a clear, kept until it is done; back on owes none")
	func clearOwed() {
		var saved = profile()
		#expect(!saved.clearsHistory)

		saved.savesHistory = false
		#expect(saved.clearsHistory)

		saved.clearsHistory = false
		saved.savesHistory = false
		#expect(!saved.clearsHistory)

		saved.savesHistory = true
		saved.savesHistory = false
		#expect(saved.clearsHistory)

		saved.savesHistory = true
		#expect(!saved.clearsHistory)
	}

	@Test("the history choice goes from the profile to the form and back")
	func historyChoice() {
		var saved = profile()
		saved.savesHistory = false

		var draft = ProfileDraft(profile: saved)
		#expect(!draft.savesHistory)

		draft.savesHistory = true
		draft.apply(to: &saved) { .preset(.macOS) }
		#expect(saved.savesHistory)
	}

	@Test("saving an IPv6 host leaves it and the credentials as they are")
	func ipv6Host() {
		var draft = ProfileDraft()
		draft.proxyHost = "[2606:4700:4700::1111]"
		draft.proxyPort = "1080"
		draft.proxyUsername = "user"

		let pasted = draft.pasteProxy(draft.proxyHost)
		#expect(!pasted)
		#expect(draft.proxyHost == "[2606:4700:4700::1111]")
		#expect(draft.proxyPort == "1080")
		#expect(draft.proxyUsername == "user")
	}

	@Test("a name is required, spaces alone are none")
	func nameRequired() {
		var draft = ProfileDraft()
		draft.name = "   "

		#expect(draft.validate() == .nameMissing)

		draft.name = " Work "
		#expect(draft.validate() == nil)
		#expect(draft.trimmedName == "Work")
	}

	@Test("a proxy with a host saves only with a port of 1 to 65535")
	func portChecked() {
		var draft = ProfileDraft()
		draft.name = "Work"
		draft.proxyEnabled = true
		draft.proxyHost = "proxy.example"

		for port in ["0", "65536", "-1", "abc", ""] {
			draft.proxyPort = port
			#expect(draft.validate() == .portInvalid, "port \(port)")
			#expect(draft.proxy == nil)
		}

		draft.proxyPort = " 65535 "
		#expect(draft.validate() == nil)
		#expect(draft.proxy?.port == 65535)
	}

	@Test("a missing name is the first problem, before the port")
	func nameFirst() {
		var draft = ProfileDraft()
		draft.proxyEnabled = true
		draft.proxyHost = "proxy.example"
		draft.proxyPort = "0"

		#expect(draft.validate() == .nameMissing)
	}

	@Test("no host, or the proxy off, saves as no proxy, whatever the port")
	func noProxy() {
		var draft = ProfileDraft()
		draft.name = "Work"
		draft.proxyEnabled = true
		draft.proxyHost = "  "
		draft.proxyPort = "0"

		#expect(draft.validate() == nil)
		#expect(draft.proxy == nil)

		draft.proxyHost = "proxy.example"
		draft.proxyEnabled = false
		#expect(draft.validate() == nil)
		#expect(draft.proxy == nil)
	}

	@Test("the proxy trims its host and leaves empty credentials out")
	func proxyFields() {
		var draft = ProfileDraft()
		draft.proxyEnabled = true
		draft.proxyType = .socks5
		draft.proxyHost = " proxy.example "
		draft.proxyPort = "1080"

		#expect(draft.proxy == ProxyConfig(proxyType: .socks5, host: "proxy.example", port: 1080))

		draft.proxyUsername = "user"
		draft.proxyPassword = "secret"
		#expect(draft.proxy?.username == "user")
		#expect(draft.proxy?.password == "secret")
	}

	@Test("a pasted proxy fills its fields; one it leaves out keeps what was typed")
	func paste() {
		var draft = ProfileDraft()
		draft.proxyUsername = "typed"
		draft.proxyPassword = "kept"

		let pasted = draft.pasteProxy("proxy.example:3128")
		#expect(pasted)
		#expect(draft.proxyHost == "proxy.example")
		#expect(draft.proxyPort == "3128")
		#expect(draft.proxyUsername == "typed")
		#expect(draft.proxyPassword == "kept")

		let withCredentials = draft.pasteProxy("host:8080:user:p:ss")
		#expect(withCredentials)
		#expect(draft.proxyUsername == "user")
		#expect(draft.proxyPassword == "p:ss")

		let badPort = draft.pasteProxy("host:70000")
		#expect(!badPort)
		#expect(draft.proxyHost == "host")
		#expect(draft.proxyPort == "8080")
	}

	@Test("a new profile gets a generated fingerprint while spoofing, none without")
	func newFingerprint() {
		var draft = ProfileDraft()
		draft.name = "Work"
		draft.platform = .windows

		var created = BrowserProfile(id: "p", name: "", createdAt: "", updatedAt: "")
		draft.apply(to: &created) { .preset(draft.platform) }
		#expect(created.fingerprint == .preset(.windows))
		#expect(created.name == "Work")

		draft.spoofing = false
		draft.apply(to: &created) { .preset(.linux) }
		#expect(created.fingerprint == nil)
	}

	@Test("changing the platform regenerates the fingerprint; a group of spaces is none")
	func editedFingerprint() {
		var saved = profile(fingerprint: .preset(.windows))
		var draft = ProfileDraft(profile: saved)
		draft.platform = .linux
		draft.group = "  "

		draft.apply(to: &saved) { .preset(.linux) }

		#expect(saved.fingerprint == .preset(.linux))
		#expect(saved.group == nil)
	}
}
