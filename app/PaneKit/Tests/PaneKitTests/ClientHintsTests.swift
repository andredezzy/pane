import Foundation
@testable import PaneKit
import Testing

private let realUA =
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/146.0.7680.65 Safari/537.36"

private func fingerprint(
	_ platform: Platform,
	userAgent: String,
	renderer: String = "ANGLE (NVIDIA, NVIDIA GeForce RTX 3060 Direct3D11 vs_5_0 ps_5_0, D3D11)"
) -> Fingerprint {
	Fingerprint(
		userAgent: userAgent,
		platform: platform,
		screen: Fingerprint.Screen(width: 1920, height: 1080),
		language: "en-US",
		languages: ["en-US", "en"],
		timezone: "America/New_York",
		webgl: Fingerprint.WebGL(vendor: "Google Inc. (NVIDIA)", renderer: renderer),
		hardwareConcurrency: 8,
		deviceMemory: 8,
		maxTouchPoints: 0
	)
}

private func serialized(_ list: [UABrand]) -> String {
	list.map { "\"\($0.brand)\";v=\"\($0.version)\"" }.joined(separator: ", ")
}

// Ported from apps/veil profile/client-hints.test.ts.
@Suite("reconcileChromeVersion")
struct ReconcileChromeVersionTests {
	@Test("rewrites a stale claimed Chrome version to the real engine's")
	func staleVersion() {
		let claimed =
			"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/136.0.0.0 Safari/537.36"

		#expect(
			reconcileChromeVersion(claimed, realUA: realUA)
				== "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/146.0.7680.65 Safari/537.36"
		)
	}

	@Test("leaves the rest of the claimed UA (platform token, Safari suffix) intact")
	func restIntact() {
		let claimed =
			"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/136.0.0.0 Safari/537.36"

		#expect(
			reconcileChromeVersion(claimed, realUA: realUA)
				== "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/146.0.7680.65 Safari/537.36"
		)
	}

	@Test("is a no-op when the claimed version already matches the engine")
	func matchingVersion() {
		#expect(reconcileChromeVersion(realUA, realUA: realUA) == realUA)
	}

	@Test("returns the claimed UA unchanged when the real UA carries no Chrome token")
	func noChromeToken() {
		let claimed =
			"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/136.0.0.0 Safari/537.36"

		#expect(reconcileChromeVersion(claimed, realUA: "Mozilla/5.0 (weird runtime)") == claimed)
	}

	@Test("replaces a bare major version too")
	func bareMajor() {
		#expect(
			reconcileChromeVersion("Mozilla/5.0 Chrome/136 Safari/537.36", realUA: realUA)
				== "Mozilla/5.0 Chrome/146.0.7680.65 Safari/537.36"
		)
	}
}

@Suite("Client Hints")
struct ClientHintsTests {
	// Chromium's own expectations (user_agent_utils_unittest.cc,
	// GenerateBrandVersionListUnbranded*).
	@Test("the brand list matches Chromium's for an unbranded build")
	func chromiumBrandList() {
		#expect(serialized(brandList(seed: 84, brand: nil, version: "84", fullVersions: false)) == #""Not;A=Brand";v="8", "Chromium";v="84""#)
		#expect(serialized(brandList(seed: 84, brand: nil, version: "84.0.0.0", fullVersions: true)) == #""Not;A=Brand";v="8.0.0.0", "Chromium";v="84.0.0.0""#)
		#expect(serialized(brandList(seed: 85, brand: nil, version: "85", fullVersions: false)) == #""Chromium";v="85", "Not=A?Brand";v="99""#)
		#expect(serialized(brandList(seed: 85, brand: nil, version: "85.0.0.0", fullVersions: true)) == #""Chromium";v="85.0.0.0", "Not=A?Brand";v="99.0.0.0""#)
	}

	// The Sec-CH-UA real Google Chrome sent at each version.
	@Test(
		"Sec-CH-UA matches Google Chrome's",
		arguments: [
			(120, #""Not_A Brand";v="8", "Chromium";v="120", "Google Chrome";v="120""#),
			(124, #""Chromium";v="124", "Google Chrome";v="124", "Not-A.Brand";v="99""#),
			(131, #""Google Chrome";v="131", "Chromium";v="131", "Not_A Brand";v="24""#),
			(136, #""Chromium";v="136", "Google Chrome";v="136", "Not.A/Brand";v="99""#),
		]
	)
	func googleChromeBrands(major: Int, header: String) {
		let hints = fingerprint(.windows, userAgent: "Mozilla/5.0 Chrome/\(major).0.0.0 Safari/537.36")
			.clientHints(engineVersion: "\(major).0.6099.71")

		#expect(clientHintHeaders(hints)["sec-ch-ua"] == header)
	}

	@Test("the full version list carries the engine's full version")
	func fullVersion() {
		let hints = fingerprint(.windows, userAgent: "Mozilla/5.0 Chrome/156.0.0.0 Safari/537.36")
			.clientHints(engineVersion: "156.0.8078.48")

		#expect(hints.uaFullVersion == "156.0.8078.48")
		#expect(serialized(hints.fullVersionList) == #""Not:A-Brand";v="8.0.0.0", "Chromium";v="156.0.8078.48", "Google Chrome";v="156.0.8078.48""#)
		#expect(serialized(hints.brands) == #""Not:A-Brand";v="8", "Chromium";v="156", "Google Chrome";v="156""#)
	}

	@Test("a claim for another major keeps its own full version")
	func otherMajor() {
		let hints = fingerprint(.windows, userAgent: "Mozilla/5.0 Chrome/136.0.7103.92 Safari/537.36")
			.clientHints(engineVersion: "156.0.8078.48")

		#expect(hints.uaFullVersion == "136.0.7103.92")
		#expect(hints.brands.contains(UABrand(brand: "Google Chrome", version: "136")))
	}

	@Test("a UA without a Chrome token falls back to Chrome 136")
	func noChromeToken() {
		let hints = fingerprint(.windows, userAgent: "Mozilla/5.0 (weird runtime)").clientHints(engineVersion: "156.0.8078.48")

		#expect(hints.uaFullVersion == "136.0.0.0")
		#expect(hints.brands.contains(UABrand(brand: "Chromium", version: "136")))
	}

	@Test("platform and platform version follow the claimed OS; Linux reports none")
	func platforms() {
		let windows = fingerprint(.windows, userAgent: realUA).clientHints(engineVersion: "146.0.7680.65")
		let macOS = fingerprint(.macOS, userAgent: realUA).clientHints(engineVersion: "146.0.7680.65")
		let linux = fingerprint(.linux, userAgent: realUA).clientHints(engineVersion: "146.0.7680.65")

		#expect((windows.platform, windows.platformVersion) == ("Windows", "15.0.0"))
		#expect((macOS.platform, macOS.platformVersion) == ("macOS", "14.6.1"))
		#expect((linux.platform, linux.platformVersion) == ("Linux", ""))
	}

	@Test("the architecture is arm for an Apple Silicon GPU, x86 otherwise")
	func architecture() {
		let appleSilicon = fingerprint(.macOS, userAgent: realUA, renderer: "ANGLE (Apple, Apple M1, OpenGL 4.1)")
		let intel = fingerprint(.macOS, userAgent: realUA, renderer: "ANGLE (Intel, Intel(R) Iris(TM) Plus Graphics, OpenGL 4.1)")

		#expect(appleSilicon.clientHints(engineVersion: "146.0.7680.65").architecture == "arm")
		#expect(intel.clientHints(engineVersion: "146.0.7680.65").architecture == "x86")
	}

	@Test("the headers serialize as Chrome writes them")
	func headers() {
		let headers = clientHintHeaders(
			fingerprint(.windows, userAgent: realUA).clientHints(engineVersion: "146.0.7680.65")
		)

		#expect(headers["sec-ch-ua-platform"] == #""Windows""#)
		#expect(headers["sec-ch-ua-platform-version"] == #""15.0.0""#)
		#expect(headers["sec-ch-ua-full-version"] == #""146.0.7680.65""#)
		#expect(headers["sec-ch-ua-arch"] == #""x86""#)
		#expect(headers["sec-ch-ua-bitness"] == #""64""#)
		#expect(headers["sec-ch-ua-mobile"] == "?0")
		#expect(headers["sec-ch-ua-wow64"] == "?0")
		#expect(headers["sec-ch-ua-model"] == #""""#)
		#expect(headers["sec-ch-ua-form-factors"] == #""Desktop""#)
	}
}

@Suite("EngineFingerprint")
struct EngineFingerprintTests {
	private let engineUA =
		"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/156.0.0.0 Safari/537.36"

	@Test("the User-Agent and its metadata follow the engine's version")
	func userAgent() {
		let engine = EngineFingerprint(
			fingerprint: fingerprint(.windows, userAgent: "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/136.0.0.0 Safari/537.36"),
			profileID: "profile-a",
			engineUserAgent: engineUA,
			engineVersion: "156.0.8078.48"
		)

		#expect(engine.userAgent == "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/156.0.0.0 Safari/537.36")
		#expect(engine.userAgentMetadata.fullVersion == "156.0.8078.48")
		#expect(engine.userAgentMetadata.platform == "Windows")
		#expect(engine.userAgentMetadata.formFactors == ["Desktop"])
		#expect(engine.renderer.platform == "Win32")
	}

	@Test("the available screen leaves room for each OS's bars")
	func screen() {
		var windows = fingerprint(.windows, userAgent: realUA)
		windows.screen = Fingerprint.Screen(width: 1920, height: 1080, colorDepth: 24)
		var macOS = fingerprint(.macOS, userAgent: realUA)
		macOS.screen = Fingerprint.Screen(width: 1440, height: 900, colorDepth: 30)

		let windowsScreen = EngineFingerprint.screen(windows)
		let macScreen = EngineFingerprint.screen(macOS)

		#expect(windowsScreen == EngineFingerprint.Screen(width: 1920, height: 1080, availWidth: 1920, availHeight: 1032, availLeft: 0, availTop: 0, colorDepth: 24))
		#expect(macScreen == EngineFingerprint.Screen(width: 1440, height: 900, availWidth: 1440, availHeight: 875, availLeft: 0, availTop: 25, colorDepth: 30))
		#expect(EngineFingerprint.screen(fingerprint(.linux, userAgent: realUA)).availHeight == 1053)
	}

	@Test("device memory is one Chrome reports: a power of two from 0.25 to 8")
	func deviceMemory() {
		#expect(EngineFingerprint.approximatedDeviceMemory(8) == 8)
		#expect(EngineFingerprint.approximatedDeviceMemory(16) == 8)
		#expect(EngineFingerprint.approximatedDeviceMemory(6) == 4)
		#expect(EngineFingerprint.approximatedDeviceMemory(1) == 1)
		#expect(EngineFingerprint.approximatedDeviceMemory(0.1) == 0.25)
	}

	@Test("the noise seed is the Electron app's hash of the profile ID")
	func noiseSeed() {
		#expect(EngineFingerprint.noiseSeed("profile-a") == 1_005_401_795)
		#expect(EngineFingerprint.noiseSeed("profile-b") == 1_005_401_794)
		#expect(EngineFingerprint.noiseSeed("c4e1a2b3-0000-4000-8000-000000000001") == 1_229_998_108)
		#expect(EngineFingerprint.noiseSeed("0b8f3c2e-5d1a-4e7b-9c6f-2a4d8e1f3b5c") == 485_437_020)
		#expect(EngineFingerprint.noiseSeed("") == 0)
	}

	@Test("pixel ratio and voices follow the claimed platform, on a Mac host")
	func platformDerived() {
		let windows = EngineFingerprint(fingerprint: fingerprint(.windows, userAgent: realUA), profileID: "a", engineUserAgent: engineUA, engineVersion: "156.0.8078.48")
		let macOS = EngineFingerprint(fingerprint: fingerprint(.macOS, userAgent: realUA), profileID: "a", engineUserAgent: engineUA, engineVersion: "156.0.8078.48")

		#expect(windows.renderer.devicePixelRatio == 1)
		#expect(windows.renderer.hideSpeechVoices)
		#expect(macOS.renderer.devicePixelRatio == 2)
		#expect(!macOS.renderer.hideSpeechVoices)
		#expect(macOS.renderer.platform == "MacIntel")
	}

	@Test("languages fall back to the language, and an empty timezone to none")
	func languagesAndTimezone() {
		var claimed = fingerprint(.linux, userAgent: realUA)
		claimed.languages = []
		claimed.language = "pt-BR"
		claimed.timezone = ""

		let engine = EngineFingerprint(fingerprint: claimed, profileID: "a", engineUserAgent: engineUA, engineVersion: "156.0.8078.48")

		#expect(engine.languages == ["pt-BR"])
		#expect(engine.renderer.locale == "pt-BR")
		#expect(engine.renderer.timezone == nil)
		#expect(engine.renderer.platform == "Linux x86_64")
	}

	@Test("the JSON is stable and decodes back")
	func json() throws {
		let engine = EngineFingerprint(fingerprint: fingerprint(.windows, userAgent: realUA), profileID: "a", engineUserAgent: engineUA, engineVersion: "156.0.8078.48")
		let text = try engine.json()

		#expect(try engine.json() == text)
		#expect(text.hasPrefix(#"{"languages":["en-US","en"],"renderer":{"audioNoise":true"#))
		#expect(try JSONDecoder().decode(EngineFingerprint.self, from: Data(text.utf8)) == engine)
	}
}
