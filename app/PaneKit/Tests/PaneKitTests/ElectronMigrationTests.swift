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

		#expect(outcome == .migrated(profiles: 1))
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

		// The Electron app's extensions stay behind: none lands in a profile.
		let record = try #require(try PersistedFile<ElectronMigration.Record>(directory: data, name: ElectronMigration.recordName).load())
		#expect(record.profiles == [id])
		#expect(!fileManager.fileExists(atPath: profile.appending(path: "Extensions").path))

		#expect(try ElectronMigration.run(from: source, into: data) == .notNeeded)
	}

	// A process that runs from an app bundle with Electron's framework in it,
	// as the Electron app does: a copy of sleep(1), signed ad hoc. With
	// Apple's signature, the copy breaks the launch constraint macOS puts on
	// its own binaries, and macOS files a crash report named Pane for it.
	private func fakeElectron(in root: URL) throws -> Process {
		let contents = root.appending(path: "Pane.app/Contents")
		let executable = contents.appending(path: "MacOS/Pane")
		try fileManager.createDirectory(at: executable.deletingLastPathComponent(), withIntermediateDirectories: true)
		try fileManager.copyItem(at: URL(fileURLWithPath: "/bin/sleep"), to: executable)

		// Signed before the framework is there: codesign checks a bundle's
		// frameworks, and an empty folder is none.
		let codesign = Process()
		codesign.executableURL = URL(fileURLWithPath: "/usr/bin/codesign")
		codesign.arguments = ["--sign", "-", "--force", executable.path]
		codesign.standardError = FileHandle.nullDevice
		try codesign.run()
		codesign.waitUntilExit()
		try #require(codesign.terminationStatus == 0)

		try fileManager.createDirectory(at: contents.appending(path: "Frameworks/Electron Framework.framework"), withIntermediateDirectories: true)

		let process = Process()
		process.executableURL = executable
		process.arguments = ["30"]
		try process.run()

		return process
	}

	@Test("waits while the Electron app runs, and does nothing without it")
	func preconditions() throws {
		let source = try electronData()
		let data = fileManager.temporaryDirectory.appending(path: "pane-native-\(UUID().uuidString)")
		let electron = try fakeElectron(in: source.appending(path: "bundle"))
		defer {
			electron.terminate()
			try? fileManager.removeItem(at: source)
			try? fileManager.removeItem(at: data)
		}

		try fileManager.createSymbolicLink(atPath: source.appending(path: "SingletonLock").path, withDestinationPath: "host-\(electron.processIdentifier)")

		#expect(try ElectronMigration.run(from: source, into: data) == .sourceInUse)
		#expect(!fileManager.fileExists(atPath: data.appending(path: "profiles.json").path))
		#expect(try ElectronMigration.run(from: source.appending(path: "missing"), into: data) == .noSource)
	}

	@Test("a stale lock whose pid is now another process does not hold it back")
	func staleLock() throws {
		let source = try electronData()
		let data = fileManager.temporaryDirectory.appending(path: "pane-native-\(UUID().uuidString)")
		defer {
			try? fileManager.removeItem(at: source)
			try? fileManager.removeItem(at: data)
		}

		// This test's own process: alive, and not an Electron app.
		try fileManager.createSymbolicLink(atPath: source.appending(path: "SingletonLock").path, withDestinationPath: "host-\(getpid())")

		#expect(try ElectronMigration.run(from: source, into: data) == .migrated(profiles: 1))
	}

	@Test("a launch that made nothing, or one cut short, still migrates")
	func notCommitted() throws {
		let source = try electronData()
		let data = fileManager.temporaryDirectory.appending(path: "pane-native-\(UUID().uuidString)")
		defer {
			try? fileManager.removeItem(at: source)
			try? fileManager.removeItem(at: data)
		}

		// What a launch beside the running Electron app saved (no profiles),
		// and what a migration killed before profiles.json moved left.
		try PersistedFile<ProfilesState>(directory: data, name: "profiles").save(ProfilesState())
		try PersistedFile<SettingsState>(directory: data, name: "settings").save(SettingsState())
		try PersistedFile<SecurityState>(directory: data, name: "security").save(SecurityState())

		#expect(try ElectronMigration.run(from: source, into: data) == .migrated(profiles: 1))

		let settings = try #require(try PersistedFile<SettingsState>(directory: data, name: "settings").load())
		#expect(settings.settings.theme == .dark)

		let security = try #require(try PersistedFile<SecurityState>(directory: data, name: "security").load())
		#expect(security.pin?.length == 4)

		let profiles = try #require(try PersistedFile<ProfilesState>(directory: data, name: "profiles").load())
		#expect(profiles.profiles.map(\.name) == ["Work"])
	}

	@Test("a record keeps whether the cookie key still waits; an older one reads as not")
	func recordCookieKey() throws {
		let data = fileManager.temporaryDirectory.appending(path: "pane-native-\(UUID().uuidString)")
		defer { try? fileManager.removeItem(at: data) }

		let file = PersistedFile<ElectronMigration.Record>(directory: data, name: ElectronMigration.recordName)
		try file.save(ElectronMigration.Record(source: "/old", migratedAt: "2026-10-27T00:00:00Z", profiles: ["a"], cookieKeyPending: true))
		#expect(try file.load()?.cookieKeyPending == true)

		try Data(#"{"state":{"source":"/old","migratedAt":"2026-10-26T00:00:00Z","profiles":["a"],"pendingExtensions":{}},"version":0}"#.utf8).write(to: file.url)
		#expect(try file.load()?.cookieKeyPending == nil)
	}

	@Test("reads the Electron app's folder used last, named after its package")
	func defaultSource() throws {
		let support = fileManager.temporaryDirectory.appending(path: "pane-support-\(UUID().uuidString)")
		defer { try? fileManager.removeItem(at: support) }

		// Neither folder: the current package's.
		var source = ElectronMigration.defaultSource(applicationSupport: support)
		#expect(source.directory.lastPathComponent == "veil")
		#expect(source.keychainService == "@pane/veil Safe Storage")

		for (name, date) in [("@pane/veil", "2026-05-01T00:00:00Z"), ("@pane/desktop", "2026-05-05T00:00:00Z")] {
			let profiles = support.appending(path: "\(name)/profiles.json")
			try fileManager.createDirectory(at: profiles.deletingLastPathComponent(), withIntermediateDirectories: true)
			try Data("{}".utf8).write(to: profiles)
			try fileManager.setAttributes([.modificationDate: try Date(date, strategy: .iso8601)], ofItemAtPath: profiles.path)
		}

		// A build from before the rename, used last.
		source = ElectronMigration.defaultSource(applicationSupport: support)
		#expect(source.appName == "@pane/desktop")
		#expect(source.directory == support.appending(path: "@pane/desktop", directoryHint: .isDirectory))
		#expect(source.keychainService == "@pane/desktop Safe Storage")

		#expect(ElectronMigration.Source(directory: support).keychainService == nil)
	}

	@Test("the native app's own profiles are never replaced")
	func existingData() throws {
		let source = try electronData()
		let data = fileManager.temporaryDirectory.appending(path: "pane-native-\(UUID().uuidString)")
		defer {
			try? fileManager.removeItem(at: source)
			try? fileManager.removeItem(at: data)
		}

		let own = BrowserProfile(id: "own", name: "Mine", createdAt: "2026-10-27T00:00:00Z", updatedAt: "2026-10-27T00:00:00Z")
		try PersistedFile<ProfilesState>(directory: data, name: "profiles").save(ProfilesState(profiles: [own]))

		#expect(try ElectronMigration.run(from: source, into: data) == .notNeeded)

		// Nor is a file it cannot read.
		try Data("{".utf8).write(to: data.appending(path: "profiles.json"))

		#expect(try ElectronMigration.run(from: source, into: data) == .notNeeded)
	}
}
