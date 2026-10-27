import Foundation
@testable import PaneKit
import Testing

@Suite("ElectronMigration")
struct ElectronMigrationTests {
	private let fileManager = FileManager.default
	private let id = "4F1C2A7E-0000-4000-8000-00000000000A"

	// An Electron data directory as the app leaves it: its JSON files, a
	// partition per profile (lowercased), and an extension.
	private func electronData() throws -> URL {
		let root = fileManager.temporaryDirectory.appending(path: "pane-electron-\(UUID().uuidString)")
		let partition = root.appending(path: "Partitions/profile-\(id.lowercased())")

		for directory in [partition.appending(path: "Local Storage/leveldb"), partition.appending(path: "Cache/Cache_Data"), root.appending(path: "Extensions/ddkjiahejlhfcafbddmgiahcphecmpfh/2025.1.1_0")] {
			try fileManager.createDirectory(at: directory, withIntermediateDirectories: true)
		}

		let profiles = #"{"state":{"profiles":[{"id":"\#(id)","name":"Work","color":"ROSE","group":null,"fingerprint":null,"proxy":null,"keepLoaded":true,"tabs":[{"id":"t1","url":"https://example.com/","title":"Example","favicon":"","isLoaded":true}],"createdAt":"2026-01-01T00:00:00.000Z","updatedAt":"2026-01-01T00:00:00.000Z"}]},"version":0}"#
		try Data(profiles.utf8).write(to: root.appending(path: "profiles.json"))
		try Data(#"{"state":{"settings":{"chromiumPath":"","theme":"dark","tabSleepAfterMinutes":5,"profileUnloadAfterMinutes":null,"cacheBudgetMB":1024}},"version":0}"#.utf8).write(to: root.appending(path: "settings.json"))
		try Data(#"{"state":{"pin":{"hash":"$2b$10$x","length":4},"failedAttempts":0},"version":0}"#.utf8).write(to: root.appending(path: "security.json"))
		try Data("cookies".utf8).write(to: partition.appending(path: "Cookies"))
		try Data("storage".utf8).write(to: partition.appending(path: "Local Storage/leveldb/000003.log"))
		try Data("cache".utf8).write(to: partition.appending(path: "Cache/Cache_Data/data_0"))
		try Data("{}".utf8).write(to: partition.appending(path: "Preferences"))
		try Data("{}".utf8).write(to: root.appending(path: "Extensions/ddkjiahejlhfcafbddmgiahcphecmpfh/2025.1.1_0/manifest.json"))

		return root
	}

	private func snapshot(_ root: URL) -> [String: Data] {
		var files: [String: Data] = [:]
		let enumerator = fileManager.enumerator(at: root, includingPropertiesForKeys: nil)

		while let url = enumerator?.nextObject() as? URL {
			files[url.path] = try? Data(contentsOf: url)
		}

		return files
	}

	@Test("moves profiles, settings, the PIN, and each profile's data; never touches the source")
	func migrates() throws {
		let source = try electronData()
		let data = fileManager.temporaryDirectory.appending(path: "pane-native-\(UUID().uuidString)")
		defer {
			try? fileManager.removeItem(at: source)
			try? fileManager.removeItem(at: data)
		}

		let before = snapshot(source)
		let outcome = try ElectronMigration.run(from: source, into: data)

		#expect(outcome == .migrated(profiles: 1, extensions: 1))
		#expect(snapshot(source) == before)

		let profiles = try #require(try PersistedFile<ProfilesState>(directory: data, name: "profiles").load())
		#expect(profiles.profiles.first?.name == "Work")
		#expect(profiles.profiles.first?.keepLoaded == true)
		#expect(profiles.profiles.first?.tabs.first?.url == "https://example.com/")

		let settings = try #require(try PersistedFile<SettingsState>(directory: data, name: "settings").load())
		#expect(settings.settings.theme == .dark)
		#expect(settings.settings.profileUnloadAfterMinutes == nil)

		let security = try #require(try PersistedFile<SecurityState>(directory: data, name: "security").load())
		#expect(security.pin?.length == 4)

		let profile = data.appending(path: "Engine/\(id)")
		#expect(fileManager.fileExists(atPath: profile.appending(path: "Cookies").path))
		#expect(fileManager.fileExists(atPath: profile.appending(path: "Local Storage/leveldb/000003.log").path))
		#expect(!fileManager.fileExists(atPath: profile.appending(path: "Cache").path))
		#expect(!fileManager.fileExists(atPath: profile.appending(path: "Preferences").path))
		#expect(!fileManager.fileExists(atPath: data.appending(path: "Migration").path))

		let record = try #require(try PersistedFile<ElectronMigration.Record>(directory: data, name: ElectronMigration.recordName).load())
		#expect(record.pendingExtensions == [id: ["ddkjiahejlhfcafbddmgiahcphecmpfh"]])

		#expect(try ElectronMigration.run(from: source, into: data) == .notNeeded)
	}

	@Test("waits while the Electron app runs, and does nothing without it")
	func preconditions() throws {
		let source = try electronData()
		let data = fileManager.temporaryDirectory.appending(path: "pane-native-\(UUID().uuidString)")
		defer {
			try? fileManager.removeItem(at: source)
			try? fileManager.removeItem(at: data)
		}

		try fileManager.createSymbolicLink(atPath: source.appending(path: "SingletonLock").path, withDestinationPath: "host-\(getpid())")

		#expect(try ElectronMigration.run(from: source, into: data) == .sourceInUse)
		#expect(!fileManager.fileExists(atPath: data.appending(path: "profiles.json").path))
		#expect(try ElectronMigration.run(from: source.appending(path: "missing"), into: data) == .noSource)
	}

	@Test("the native app's own data is never replaced")
	func existingData() throws {
		let source = try electronData()
		let data = fileManager.temporaryDirectory.appending(path: "pane-native-\(UUID().uuidString)")
		defer {
			try? fileManager.removeItem(at: source)
			try? fileManager.removeItem(at: data)
		}

		try PersistedFile<ProfilesState>(directory: data, name: "profiles").save(ProfilesState())

		#expect(try ElectronMigration.run(from: source, into: data) == .notNeeded)
	}
}
