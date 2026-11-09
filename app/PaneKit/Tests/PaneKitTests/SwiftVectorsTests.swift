import CryptoKit
import Foundation
import PaneKit
import Testing

// What the Swift Pane writes, kept for the C++ port to read byte for byte
// (docs/plans/views-migration.md, C1): pane/model/test_data/swift/, the files
// as PaneKit saves them (profiles.json sealed, settings.json and security.json
// plain), and vectors.json, the inputs and the keys derived from them.
// PANE_WRITE_SWIFT_VECTORS=1 writes them again, with new salts and nonces;
// otherwise each one is read back here, so they stay the Swift's.
@Suite("Swift vectors", .serialized)
struct SwiftVectorsTests {
	private static let directory = URL(fileURLWithPath: #filePath)
		.deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
		.deletingLastPathComponent().deletingLastPathComponent()
		.appending(path: "pane/model/test_data/swift")

	// The data key the vectors use, and the Keychain item's stored text, which
	// the sealing key takes as it is, not decoded (SafeStorage).
	private static let dataKey = SymmetricKey(data: Data(repeating: 7, count: 32))
	private static let secret = Data("B5u8Rrq0X0E2h3dPl5M1cQ==".utf8)
	private static let pin = "908172"
	private static let profileID = "6f9d2c1a-8b3e-4c7d-9a2f-1e5b7c3d9f02"

	private struct Vectors: Codable, Equatable {
		var dataKey: String
		var secret: String
		var pin: String
		var profile: String
		var filesKey: String
		var engineKey: String
		var volumePassphrase: String
		var formerVolumePassphrase: String
		var sealedWithPin: DataKey.Sealed
		var sealedWithoutPin: DataKey.Sealed
	}

	private static func hex(_ key: SymmetricKey) -> String {
		key.withUnsafeBytes { Data($0) }.map { String(format: "%02x", $0) }.joined()
	}

	// Two profiles: one as a new profile starts, one with every field set,
	// with text a JSON writer escapes ("/", "<", and beyond ASCII).
	static let profiles = ProfilesState(profiles: [
		BrowserProfile(
			id: profileID,
			name: "Personal",
			tabs: [Tab(id: "9b1f3c2e-1d4a-4f6b-8c7e-2a5d9e0f1b3c", url: "https://www.google.com/")],
			createdAt: "2026-11-09T04:00:00.000Z",
			updatedAt: "2026-11-09T04:00:00.000Z"
		),
		BrowserProfile(
			id: "0c4e8a2b-7f1d-4e3a-b6c9-5d2f8a1e7b40",
			name: "Café <Work> / ✓",
			color: .violet,
			group: "Clients",
			fingerprint: Fingerprint(
				userAgent: "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/158.0.0.0 Safari/537.36",
				platform: .windows,
				screen: Fingerprint.Screen(width: 1920, height: 1080),
				language: "pt-BR",
				languages: ["pt-BR", "pt", "en"],
				timezone: "America/Sao_Paulo",
				webgl: Fingerprint.WebGL(vendor: "Google Inc. (NVIDIA)", renderer: "ANGLE (NVIDIA GeForce RTX 3060)"),
				hardwareConcurrency: 8,
				deviceMemory: 8,
				maxTouchPoints: 0,
				canvas: Fingerprint.Noise(noise: false)
			),
			proxy: ProxyConfig(proxyType: .socks5, host: "proxy.example.com", port: 1080, username: "user", password: "p@ss/<word>"),
			keepLoaded: true,
			savesHistory: false,
			tabs: [
				Tab(id: "3e7a1c9d-2b4f-4a6e-8d1c-7f3b5a9e2c10", url: "https://example.com/a?b=c&d=</script>", title: "Example — “quoted”", favicon: "data:image/png;base64,iVBORw0KGgo="),
				Tab(id: "5c2e9a7b-1f3d-4b8e-a6c2-9d4f1b7e3a58", url: "about:blank"),
			],
			createdAt: "2026-10-01T12:30:00.000Z",
			updatedAt: "2026-11-08T23:59:59.999Z"
		),
	])

	static let settings = SettingsState(settings: AppSettings(
		theme: .dark,
		appIcon: .dusk,
		tabSleepAfterMinutes: nil,
		profileUnloadAfterMinutes: 45,
		cacheBudgetMB: 500,
		lockAfterMinutes: 5,
		sidebarWidth: 312.5
	))

	@Test("the files and keys the Swift writes")
	func vectors() throws {
		let vectorsFile = Self.directory.appending(path: "vectors.json")
		let profilesFile = SealedFile<ProfilesState>(directory: Self.directory, name: "profiles")
		let settingsFile = PersistedFile<SettingsState>(directory: Self.directory, name: "settings")
		let securityFile = PersistedFile<SecurityState>(directory: Self.directory, name: "security")
		let filesKey = DataKey.filesKey(Self.dataKey)

		if ProcessInfo.processInfo.environment["PANE_WRITE_SWIFT_VECTORS"] == "1" {
			let withPin = try DataKey.seal(Self.dataKey, secret: Self.secret, pin: Self.pin)
			let vectors = Vectors(
				dataKey: Self.dataKey.withUnsafeBytes { Data($0) }.base64EncodedString(),
				secret: String(decoding: Self.secret, as: UTF8.self),
				pin: Self.pin,
				profile: Self.profileID,
				filesKey: Self.hex(filesKey),
				engineKey: Self.hex(DataKey.engineKey(Self.dataKey, profile: Self.profileID)),
				volumePassphrase: DataKey.volumePassphrase(Self.dataKey),
				formerVolumePassphrase: DataKey.formerVolumePassphrase(Self.dataKey),
				sealedWithPin: withPin,
				sealedWithoutPin: try DataKey.seal(Self.dataKey, secret: Self.secret, pin: nil)
			)
			let encoder = JSONEncoder()
			encoder.outputFormatting = [.sortedKeys, .prettyPrinted]

			try FileManager.default.createDirectory(at: Self.directory, withIntermediateDirectories: true)
			try encoder.encode(vectors).write(to: vectorsFile, options: .atomic)
			try profilesFile.save(Self.profiles, key: filesKey)
			try settingsFile.save(Self.settings)
			try securityFile.save(SecurityState(pin: Pin(length: 6), failedAttempts: 2, key: withPin))
		}

		let vectors = try JSONDecoder().decode(Vectors.self, from: Data(contentsOf: vectorsFile))
		let bytes = { (key: SymmetricKey) in key.withUnsafeBytes { Data($0) } }

		#expect(vectors.filesKey == Self.hex(filesKey))
		#expect(vectors.engineKey == Self.hex(DataKey.engineKey(Self.dataKey, profile: Self.profileID)))
		#expect(vectors.volumePassphrase == DataKey.volumePassphrase(Self.dataKey))
		#expect(vectors.formerVolumePassphrase == DataKey.formerVolumePassphrase(Self.dataKey))
		#expect(try bytes(DataKey.open(vectors.sealedWithPin, secret: Self.secret, pin: Self.pin)) == bytes(Self.dataKey))
		#expect(try bytes(DataKey.open(vectors.sealedWithoutPin, secret: Self.secret, pin: nil)) == bytes(Self.dataKey))
		#expect(try profilesFile.load(key: filesKey) == Self.profiles)
		#expect(try settingsFile.load() == Self.settings)

		let security = try #require(try securityFile.load())
		#expect(security.pin == Pin(length: 6))
		#expect(security.failedAttempts == 2)
		#expect(security.key == vectors.sealedWithPin)
	}

	// The same files as the C++ port writes them, from these
	// (pane/model/data_key_unittest.cc, CppWrites): the Swift Pane reads each.
	@Test("the files the C++ port writes")
	func cppWrites() throws {
		let directory = Self.directory.deletingLastPathComponent().appending(path: "cpp")
		let filesKey = DataKey.filesKey(Self.dataKey)

		#expect(try SealedFile<ProfilesState>(directory: directory, name: "profiles").load(key: filesKey) == Self.profiles)
		#expect(try PersistedFile<SettingsState>(directory: directory, name: "settings").load() == Self.settings)

		let security = try #require(try PersistedFile<SecurityState>(directory: directory, name: "security").load())
		#expect(security.pin == Pin(length: 6))
		#expect(security.failedAttempts == 2)
		let key = try DataKey.open(try #require(security.key), secret: Self.secret, pin: Self.pin)
		#expect(key.withUnsafeBytes { Data($0) } == Self.dataKey.withUnsafeBytes { Data($0) })
	}
}
