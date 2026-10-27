import PaneKit
import Testing

@Suite("ProfileForm")
struct ProfileFormTests {
	private let hostGPU = Fingerprint.WebGL(vendor: "Google Inc. (Apple)", renderer: "ANGLE (Apple, ANGLE Metal Renderer: Apple M2, Unspecified Version)")

	@Test("each platform's preset is the Electron app's")
	func presets() {
		#expect(Fingerprint.preset(.windows).screen == .init(width: 1920, height: 1080, colorDepth: 24))
		#expect(Fingerprint.preset(.macOS).screen == .init(width: 1440, height: 900, colorDepth: 30))
		#expect(Fingerprint.preset(.linux).hardwareConcurrency == 4)
		#expect(Platform.allCases.allSatisfy { Fingerprint.preset($0).platform == $0 })
	}

	@Test("the host's own platform claims the host's GPU; another keeps the preset's")
	func hostGPUClaim() {
		#expect(Fingerprint.generated(for: .macOS, hostWebGL: hostGPU).webgl == hostGPU)
		#expect(Fingerprint.generated(for: .windows, hostWebGL: hostGPU).webgl == Fingerprint.preset(.windows).webgl)
		#expect(Fingerprint.generated(for: .macOS, hostWebGL: nil).webgl == Fingerprint.preset(.macOS).webgl)
	}

	@Test("an edit keeps the stored fingerprint unless spoofing or the platform changed")
	func edits() {
		var stored = Fingerprint.preset(.windows)
		stored.timezone = "Europe/Lisbon"
		let fresh = Fingerprint.preset(.linux)

		#expect(Fingerprint.edited(stored: stored, spoofing: true, platform: .windows) { fresh } == stored)
		#expect(Fingerprint.edited(stored: stored, spoofing: true, platform: .linux) { fresh } == fresh)
		#expect(Fingerprint.edited(stored: stored, spoofing: false, platform: .windows) { fresh } == nil)
		#expect(Fingerprint.edited(stored: nil, spoofing: true, platform: .linux) { fresh } == fresh)
		#expect(Fingerprint.edited(stored: nil, spoofing: false, platform: .linux) { fresh } == nil)
	}

	@Test("the proxy shortcut fills host, port, and the credentials it has")
	func proxyString() {
		#expect(ProxyString("10.0.0.2:3128") == ProxyString(host: "10.0.0.2", port: 3128))
		#expect(ProxyString(" proxy.example:1080:user:p4ss ")?.username == "user")
		#expect(ProxyString("proxy.example:1080:user:p4ss")?.password == "p4ss")
		#expect(ProxyString("proxy.example:1080:user")?.password == nil)
		#expect(ProxyString("proxy.example") == nil)
		#expect(ProxyString("proxy.example:port") == nil)
		#expect(ProxyString(":8080") == nil)
	}
}

extension ProxyString {
	init(host: String, port: Int) {
		self = ProxyString("\(host):\(port)")!
	}
}
