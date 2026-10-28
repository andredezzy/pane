import Foundation

// The first launch after the Electron app: its profiles, tabs, settings, PIN,
// and each profile's site data (cookies, storage) move into the native app.
// Its extensions do not: it installed each once, for every profile, and a
// native profile has only the extensions added to it. It always reads a copy
// of the Electron app's data, never the data itself, and never writes there
// (only the panic wipe erases it: PanicWipe.ElectronData).
// It runs until it has migrated: a launch that found the Electron app
// running, or failed, wrote nothing that stops the next one.
public enum ElectronMigration {
	// What a migration did, kept in the data directory as migration.json so it
	// runs once. The panic wipe leaves it, so a wiped Pane never brings the
	// Electron app's data back. A record from before extensions stayed behind
	// still lists them as pendingExtensions, which nothing reads.
	public struct Record: Codable, Hashable, Sendable {
		public var source: String
		public var migratedAt: String
		public var profiles: [String]
		// The Electron app's cookie key still waits to go to the engine: the
		// Keychain could not ask the user (a locked session, say). nil in a
		// record from before this was kept.
		public var cookieKeyPending: Bool?

		public init(source: String, migratedAt: String, profiles: [String], cookieKeyPending: Bool? = nil) {
			self.source = source
			self.migratedAt = migratedAt
			self.profiles = profiles
			self.cookieKeyPending = cookieKeyPending
		}
	}

	public enum Outcome: Equatable, Sendable {
		// The native app has profiles of its own, or migrated before.
		case notNeeded
		// No Electron app data where it lives.
		case noSource
		// The Electron app is running: its files may be mid-write.
		case sourceInUse
		case migrated(profiles: Int)
	}

	// Where the Electron app keeps its data, and the name it runs under.
	public struct Source: Equatable, Sendable {
		public var directory: URL
		// Names the Electron app's Keychain item; nil for a folder named by
		// hand (PANE_ELECTRON_DATA_DIR).
		public var appName: String?

		public init(directory: URL, appName: String? = nil) {
			self.directory = directory
			self.appName = appName
		}

		// The item whose key encrypts the Electron app's cookies:
		// "<app name> Safe Storage", with the app name as its account.
		public var keychainService: String? {
			appName.map { "\($0) Safe Storage" }
		}
	}

	// The names the packaged Electron app ran under. apps/veil has no
	// productName, so Electron names its data folder and its Keychain item
	// after the package: @pane/veil, and @pane/desktop before the package's
	// rename (2026-05-05).
	public static let appNames = ["@pane/veil", "@pane/desktop"]

	// The Electron app's data: of its folders, the one used last.
	public static func defaultSource(applicationSupport: URL) -> Source {
		let sources = appNames.map {
			Source(directory: applicationSupport.appending(path: $0, directoryHint: .isDirectory), appName: $0)
		}

		return sources.max { lastUsed($0.directory) < lastUsed($1.directory) } ?? sources[0]
	}

	static func lastUsed(_ directory: URL) -> Date {
		let profiles = directory.appending(path: "profiles.json")

		return (try? profiles.resourceValues(forKeys: [.contentModificationDateKey]))?.contentModificationDate ?? .distantPast
	}

	public static let recordName = "migration"

	// What a profile's partition holds that a browser cache can rebuild: not
	// worth copying.
	static let caches: Set<String> = [
		"Cache", "Code Cache", "GPUCache", "DawnGraphiteCache", "DawnWebGPUCache",
		"GrShaderCache", "ShaderCache", "Crashpad",
	]

	// Files that belong to the Electron session, not to the profile's data.
	static let sessionFiles: Set<String> = ["Preferences", "LOCK", "lockfile"]

	public static func run(from source: URL, into data: URL, now: Date = Date()) throws -> Outcome {
		let fileManager = FileManager.default

		guard !fileManager.fileExists(atPath: data.appending(path: "\(recordName).json").path), !hasProfiles(data) else {
			return .notNeeded
		}

		guard fileManager.fileExists(atPath: source.appending(path: "profiles.json").path) else {
			return .noSource
		}

		guard !isRunning(source) else {
			return .sourceInUse
		}

		// 1. The copy.
		let staging = data.appending(path: "Migration", directoryHint: .isDirectory)
		try? fileManager.removeItem(at: staging)
		try fileManager.createDirectory(at: staging, withIntermediateDirectories: true)
		defer { try? fileManager.removeItem(at: staging) }

		for name in ["profiles", "settings", "security"] {
			let file = source.appending(path: "\(name).json")

			if fileManager.fileExists(atPath: file.path) {
				try fileManager.copyItem(at: file, to: staging.appending(path: "\(name).json"))
			}
		}

		guard let profiles = try PersistedFile<ProfilesState>(directory: staging, name: "profiles").load()?.profiles else {
			return .noSource
		}

		for profile in profiles {
			// Electron names a partition directory in lowercase.
			let partition = source.appending(path: "Partitions/profile-\(profile.id.lowercased())", directoryHint: .isDirectory)

			if fileManager.fileExists(atPath: partition.path) {
				try copyData(of: partition, to: staging.appending(path: "Partitions/\(profile.id)", directoryHint: .isDirectory))
			}
		}

		// 2. The import, from the copy only.
		try fileManager.createDirectory(at: data.appending(path: "Engine", directoryHint: .isDirectory), withIntermediateDirectories: true)

		for profile in profiles {
			let copied = staging.appending(path: "Partitions/\(profile.id)", directoryHint: .isDirectory)

			if fileManager.fileExists(atPath: copied.path) {
				// A migration cut short left it there; profiles.json, written
				// last, was not.
				let destination = data.appending(path: "Engine/\(profile.id)", directoryHint: .isDirectory)
				try? fileManager.removeItem(at: destination)
				try fileManager.moveItem(at: copied, to: destination)
			}
		}

		// One left by a migration cut short, or by a launch that made only
		// settings, is replaced. profiles.json goes last: it commits.
		for name in ["settings", "security", "profiles"] {
			let file = staging.appending(path: "\(name).json")
			let destination = data.appending(path: "\(name).json")

			if fileManager.fileExists(atPath: file.path) {
				try? fileManager.removeItem(at: destination)
				try fileManager.moveItem(at: file, to: destination)
			}
		}

		let record = Record(
			source: source.path,
			migratedAt: ISO8601DateFormatter().string(from: now),
			profiles: profiles.map(\.id)
		)
		try PersistedFile<Record>(directory: data, name: recordName).save(record)

		return .migrated(profiles: profiles.count)
	}

	// Copies a partition's data, leaving out caches and the Electron
	// session's own files.
	static func copyData(of partition: URL, to destination: URL) throws {
		let fileManager = FileManager.default
		try fileManager.createDirectory(at: destination, withIntermediateDirectories: true)

		for entry in try fileManager.contentsOfDirectory(at: partition, includingPropertiesForKeys: nil) {
			let name = entry.lastPathComponent

			guard !caches.contains(name), !sessionFiles.contains(name) else {
				continue
			}

			try fileManager.copyItem(at: entry, to: destination.appending(path: name))
		}
	}

	// Profiles of the native app's own, which the migration never replaces.
	// An empty list is none: a launch that made nothing still migrates. One
	// that cannot be read counts as some.
	static func hasProfiles(_ data: URL) -> Bool {
		do {
			return try !(PersistedFile<ProfilesState>(directory: data, name: "profiles").load()?.profiles.isEmpty ?? true)
		} catch {
			return true
		}
	}

	// Chromium marks a running instance with a SingletonLock link to
	// "<host>-<pid>". The Electron app runs while that pid is alive (EPERM:
	// alive, another user's) and is an Electron app: a stale lock whose pid
	// came back as another process would hold the migration back for good.
	public static func isRunning(_ source: URL) -> Bool {
		guard
			let target = try? FileManager.default.destinationOfSymbolicLink(atPath: source.appending(path: "SingletonLock").path),
			let pid = target.split(separator: "-").last.flatMap({ Int32($0) }),
			kill(pid, 0) == 0 || errno == EPERM
		else {
			return false
		}

		return isElectronApp(pid)
	}

	// Whether the process runs from an app bundle with Electron's framework
	// in it.
	static func isElectronApp(_ pid: pid_t) -> Bool {
		var path = [CChar](repeating: 0, count: 4 * Int(MAXPATHLEN))

		let length = proc_pidpath(pid, &path, UInt32(path.count))

		guard length > 0 else {
			return false
		}

		// <app>/Contents/MacOS/<executable>
		let executable = String(decoding: path.prefix(Int(length)).map { UInt8(bitPattern: $0) }, as: UTF8.self)
		let contents = URL(fileURLWithPath: executable).deletingLastPathComponent().deletingLastPathComponent()

		return FileManager.default.fileExists(atPath: contents.appending(path: "Frameworks/Electron Framework.framework").path)
	}
}
