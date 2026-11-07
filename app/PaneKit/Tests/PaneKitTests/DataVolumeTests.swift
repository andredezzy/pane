import Foundation
import PaneKit
import Testing

// The volume's links and moves, on a plain folder standing in for the
// attached volume: the disk image itself is the scenarios' (security).
@Suite("DataVolume")
struct DataVolumeTests {
	private let fileManager = FileManager.default

	private func root() throws -> URL {
		let root = fileManager.temporaryDirectory.appending(path: "pane-volume-\(UUID().uuidString)")
		try fileManager.createDirectory(at: root, withIntermediateDirectories: true)
		return root
	}

	private func link(_ url: URL) -> String? {
		try? fileManager.destinationOfSymbolicLink(atPath: url.path)
	}

	private func exists(_ url: URL) -> Bool {
		fileManager.fileExists(atPath: url.path)
	}

	@Test("a new profile is marked encrypted before its first file, and opens again with its Preferences")
	func newProfileIsMarked() throws {
		let root = try root()
		defer { try? fileManager.removeItem(at: root) }
		let volume = DataVolume(dataDirectory: root, cacheDirectory: nil)
		let folder = volume.folder(profile: "p")

		try volume.prepare(profile: "p")
		#expect(exists(folder.appending(path: ".files-encrypted")))

		try Data("{}".utf8).write(to: folder.appending(path: "Profile/Preferences"))
		try volume.prepare(profile: "p")
		#expect(exists(folder.appending(path: "Profile/Preferences")))
	}

	// What 0.14.0 to 0.17.2 wrote, in a volume that opened: nothing of it may reach
	// the engine, which stops at a plain store.
	@Test("a profile with its Preferences and no marker is refused, and nothing of it changes")
	func unconvertedProfileRefused() throws {
		let root = try root()
		defer { try? fileManager.removeItem(at: root) }
		let volume = DataVolume(dataDirectory: root, cacheDirectory: nil)
		let folder = volume.folder(profile: "p")
		let kept = ["Profile/Preferences", "Profile/Cookies", "SafetyCopy"].map { folder.appending(path: $0) }

		try fileManager.createDirectory(at: folder.appending(path: "Profile"), withIntermediateDirectories: true)

		for file in kept {
			try Data("kept".utf8).write(to: file)
		}

		#expect(throws: DataVolume.BeforeTheFloor.self) {
			try volume.prepare(profile: "p", savesHistory: false)
		}

		#expect(kept.allSatisfy { (try? Data(contentsOf: $0)) == Data("kept".utf8) })
		#expect(!exists(folder.appending(path: ".files-encrypted")))
		#expect(!exists(folder.appending(path: "Cache")))
		#expect(link(root.appending(path: "Engine/p")) == nil)
	}

	@Test("what the conversion left goes at a converted profile's open, and nothing else")
	func conversionLeftoversGo() throws {
		let root = try root()
		defer { try? fileManager.removeItem(at: root) }
		let volume = DataVolume(dataDirectory: root, cacheDirectory: nil)
		let folder = volume.folder(profile: "p")

		try fileManager.createDirectory(at: folder.appending(path: "Profile"), withIntermediateDirectories: true)

		let left = ["SafetyCopy", ".files-repaired", ".safety-started", ".safety-killed", ".safety-restored", ".safety-opened-1", ".safety-opening", ".safety-failed"]
			.map { folder.appending(path: $0) }
		let kept = [".files-encrypted", "Profile/Preferences", "Profile/SafetyCopy"].map { folder.appending(path: $0) }

		for file in left + kept {
			try Data("x".utf8).write(to: file)
		}

		try volume.prepare(profile: "p")

		#expect(left.allSatisfy { !exists($0) })
		#expect(kept.allSatisfy { exists($0) })
	}

	@Test("a volume a lock could not detach opens as it is")
	func openTwice() throws {
		let root = try root()
		defer { try? fileManager.removeItem(at: root) }
		let volume = DataVolume(dataDirectory: root, cacheDirectory: nil)
		let devices = try volume.attachOrMake(passphrase: "pass")
		defer { try? volume.detach(device: devices.image, force: true) }

		try volume.open(devices, passphrase: "pass")
		#expect(volume.isMounted)
		try volume.open(devices, passphrase: "pass")
		#expect(volume.isMounted)
	}

	// What 0.14.0 to 0.17.2 made: the passphrase ADR 0121 moved away from.
	@Test("a volume on another passphrase opens nothing and changes nothing: that passphrase still opens it")
	func volumeOnAFormerPassphrase() throws {
		let root = try root()
		defer { try? fileManager.removeItem(at: root) }
		let volume = DataVolume(dataDirectory: root, cacheDirectory: nil)
		let made = try volume.attachOrMake(passphrase: "former")
		defer { volume.detachLeftovers() }

		#expect(throws: DataVolume.Failure.self) {
			try volume.open(made, passphrase: "current")
		}

		#expect(!volume.isMounted)
		try volume.detach(device: made.image, force: true)

		let again = try volume.attachImageOnce()
		#expect(throws: DataVolume.Failure.self) {
			try volume.open(again, passphrase: "current")
		}

		try volume.open(again, passphrase: "former")
		#expect(volume.isMounted)
	}

	@Test("a profile's engine folder and caches are links into the volume")
	func prepare() throws {
		let root = try root()
		defer { try? fileManager.removeItem(at: root) }
		let data = root.appending(path: "data")
		let caches = root.appending(path: "caches")
		let volume = DataVolume(dataDirectory: data, cacheDirectory: caches)

		try volume.prepare(profile: "p")
		// Twice changes nothing.
		try volume.prepare(profile: "p")
		try Data("cookie".utf8).write(to: data.appending(path: "Engine/p/Cookies"))

		#expect(link(data.appending(path: "Engine/p")) == volume.folder(profile: "p").appending(path: "Profile").path)
		#expect(link(caches.appending(path: "Engine/p")) == volume.folder(profile: "p").appending(path: "Cache").path)
		#expect(fileManager.fileExists(atPath: volume.folder(profile: "p").appending(path: "Profile/Cookies").path))
		#expect(!volume.isMounted)
	}

	@Test("a profile that saves no history opens without its earlier downloads' log or caches")
	func downloadCache() throws {
		let root = try root()
		defer { try? fileManager.removeItem(at: root) }
		let data = root.appending(path: "data")
		let volume = DataVolume(dataDirectory: data, cacheDirectory: nil)

		try volume.prepare(profile: "p")

		let folder = volume.folder(profile: "p")
		let written = ["Profile/shared_proto_db", "Profile/Cache", "Profile/Code Cache", "Cache/Cache", "Cache/Code Cache"].map { folder.appending(path: $0) }
		let kept = folder.appending(path: "Profile/Local Storage")

		for item in written + [kept] {
			try fileManager.createDirectory(at: item, withIntermediateDirectories: true)
		}

		try volume.prepare(profile: "p", savesHistory: true)
		#expect(written.allSatisfy { fileManager.fileExists(atPath: $0.path) })

		try volume.prepare(profile: "p", savesHistory: false)
		#expect(written.allSatisfy { !fileManager.fileExists(atPath: $0.path) })
		#expect(fileManager.fileExists(atPath: kept.path))
	}

	@Test("a link to somewhere else is replaced")
	func relink() throws {
		let root = try root()
		defer { try? fileManager.removeItem(at: root) }
		let data = root.appending(path: "data")
		let volume = DataVolume(dataDirectory: data, cacheDirectory: nil)

		try fileManager.createDirectory(at: data.appending(path: "Engine"), withIntermediateDirectories: true)
		try fileManager.createSymbolicLink(at: data.appending(path: "Engine/p"), withDestinationURL: root.appending(path: "elsewhere"))
		try volume.prepare(profile: "p")

		#expect(link(data.appending(path: "Engine/p")) == volume.folder(profile: "p").appending(path: "Profile").path)
	}

	@Test("an earlier Pane's plain folders move in, whole, and wait for the wipe's erase")
	func moveIn() throws {
		let root = try root()
		defer { try? fileManager.removeItem(at: root) }
		let data = root.appending(path: "data")
		let caches = root.appending(path: "caches")
		let volume = DataVolume(dataDirectory: data, cacheDirectory: caches)
		let plain = data.appending(path: "Engine/p")

		try fileManager.createDirectory(at: plain.appending(path: "Local Storage/leveldb"), withIntermediateDirectories: true)
		try Data(repeating: 7, count: 300_000).write(to: plain.appending(path: "Local Storage/leveldb/000003.log"))
		try Data("cookie".utf8).write(to: plain.appending(path: "Cookies"))
		try fileManager.createSymbolicLink(atPath: plain.appending(path: "SingletonCookie").path, withDestinationPath: "12345")
		try fileManager.createDirectory(at: caches.appending(path: "Engine/p/Cache"), withIntermediateDirectories: true)
		try Data("page".utf8).write(to: caches.appending(path: "Engine/p/Cache/entry"))

		var shares: [Double] = []
		try volume.moveIn(profiles: ["p", "never-opened"]) { shares.append($0) }

		let moved = volume.folder(profile: "p").appending(path: "Profile")
		#expect(try Data(contentsOf: moved.appending(path: "Local Storage/leveldb/000003.log")) == Data(repeating: 7, count: 300_000))
		#expect(try Data(contentsOf: moved.appending(path: "Cookies")) == Data("cookie".utf8))
		#expect(link(moved.appending(path: "SingletonCookie")) == "12345")
		#expect(link(plain) == moved.path)
		#expect(shares.last == 1)
		#expect(shares == shares.sorted())
		#expect((try? fileManager.attributesOfItem(atPath: caches.appending(path: "Engine/p").path)) == nil)

		// Staged for the erase, data and caches alike, which then takes them.
		let staged = PanicWipe.staged(dataDirectory: data, cacheDirectory: caches)
		#expect(staged.map(\.passes).sorted() == [1, 3])
		#expect(PanicWipe.eraseStaged(dataDirectory: data, cacheDirectory: caches).isEmpty)
		#expect(PanicWipe.staged(dataDirectory: data, cacheDirectory: caches).isEmpty)

		// The caches' link comes with the profile's next prepare.
		try volume.prepare(profile: "p")
		#expect(link(caches.appending(path: "Engine/p")) == volume.folder(profile: "p").appending(path: "Cache").path)
	}

	@Test("a move cut short before its stage copies the folder again")
	func moveAgain() throws {
		let root = try root()
		defer { try? fileManager.removeItem(at: root) }
		let data = root.appending(path: "data")
		let volume = DataVolume(dataDirectory: data, cacheDirectory: nil)
		let plain = data.appending(path: "Engine/p")

		try fileManager.createDirectory(at: plain, withIntermediateDirectories: true)
		try Data("whole".utf8).write(to: plain.appending(path: "Cookies"))
		// What a copy cut short left.
		try fileManager.createDirectory(at: volume.folder(profile: "p").appending(path: "Profile"), withIntermediateDirectories: true)
		try Data("half".utf8).write(to: volume.folder(profile: "p").appending(path: "Profile/Partial"))

		try volume.moveIn(profiles: ["p"]) { _ in }

		#expect(try Data(contentsOf: volume.folder(profile: "p").appending(path: "Profile/Cookies")) == Data("whole".utf8))
		#expect(!fileManager.fileExists(atPath: volume.folder(profile: "p").appending(path: "Profile/Partial").path))
	}

	@Test("an earlier engine's database of the pages visited, and its histograms, are staged for the erase")
	func engineTraces() throws {
		let root = try root()
		defer { try? fileManager.removeItem(at: root) }
		let traces = root.appending(path: "Engine/segmentation_platform/ukm_db")
		try fileManager.createDirectory(at: traces, withIntermediateDirectories: true)
		try Data("https://mail.example.com/".utf8).write(to: traces.appending(path: "000003.log"))
		let metrics = root.appending(path: "Engine/BrowserMetrics")
		try fileManager.createDirectory(at: metrics, withIntermediateDirectories: true)
		try Data(count: 4096).write(to: metrics.appending(path: "BrowserMetrics-6A1B2C3D-6C80.pma"))
		try Data(count: 4096).write(to: root.appending(path: "Engine/BrowserMetrics-spare.pma"))
		try fileManager.createDirectory(at: root.appending(path: "Engine/GraphiteDawnCache"), withIntermediateDirectories: true)

		#expect(PanicWipe.stageEngineTraces(dataDirectory: root).isEmpty)

		for trace in ["segmentation_platform", "BrowserMetrics", "BrowserMetrics-spare.pma"] {
			#expect(!fileManager.fileExists(atPath: root.appending(path: "Engine/\(trace)").path), "\(trace) stayed")
		}

		// The GPU's shader caches are not the user's, and stay.
		#expect(fileManager.fileExists(atPath: root.appending(path: "Engine/GraphiteDawnCache").path))
		#expect(PanicWipe.staged(dataDirectory: root, cacheDirectory: nil).count == 1)
		#expect(PanicWipe.eraseStaged(dataDirectory: root, cacheDirectory: nil).isEmpty)
		// Nothing to stage the next time.
		#expect(PanicWipe.stageEngineTraces(dataDirectory: root).isEmpty)
	}

	@Test("the wipe erases the volume's image without passes, and the key's copy before security.json")
	func wipe() throws {
		let data = URL(fileURLWithPath: "/tmp/pane")
		let targets = PanicWipe.targets(dataDirectory: data, cacheDirectory: nil).map(\.lastPathComponent)

		#expect(targets.contains("Data.sparsebundle"))
		#expect(Array(targets.suffix(2)) == ["Data.security.json", "security.json"])
	}
}

@Suite("LocalState")
struct LocalStateTests {
	@Test("the engine starts on its Default profile, with no profile of Pane's open last, and keeps no time of use")
	func startsOnDefault() throws {
		let state = #"{"profile":{"last_used":"0b1c3f2a-6a1d-4c1b-9d2e-1f0a2b3c4d5e","last_active_profiles":["0b1c3f2a-6a1d-4c1b-9d2e-1f0a2b3c4d5e"],"info_cache":{"Default":{"name":"Person 1"},"0b1c3f2a-6a1d-4c1b-9d2e-1f0a2b3c4d5e":{"name":"Person 2","active_time":1793000000.5}}},"tab_stats":{"max_tabs_per_window":21},"browser":{"enabled_labs_experiments":[]},"profile_counts_reported":"13437866866342267"}"#
		let changed = try #require(try LocalState.startingOnDefaultProfile(Data(state.utf8)))
		let json = try #require(try JSONSerialization.jsonObject(with: changed) as? [String: Any])
		let profile = try #require(json["profile"] as? [String: Any])

		#expect(profile["last_used"] as? String == "Default")
		#expect((profile["last_active_profiles"] as? [Any])?.isEmpty == true)
		// No time or count of use, outside the volume.
		let profiles = try #require(profile["info_cache"] as? [String: Any])
		#expect((profiles["0b1c3f2a-6a1d-4c1b-9d2e-1f0a2b3c4d5e"] as? [String: Any])?["active_time"] == nil)
		#expect(json["tab_stats"] == nil)
		// Nothing else changes.
		#expect((profiles["Default"] as? [String: Any])?["name"] as? String == "Person 1")
		#expect((profiles["0b1c3f2a-6a1d-4c1b-9d2e-1f0a2b3c4d5e"] as? [String: Any])?["name"] as? String == "Person 2")
		#expect(json["profile_counts_reported"] as? String == "13437866866342267")
		#expect(json["browser"] != nil)

		// Once it does, nothing more is written.
		#expect(try LocalState.startingOnDefaultProfile(changed) == nil)
	}
}

@Suite("BundleIdentifier")
struct BundleIdentifierTests {
	private let fileManager = FileManager.default

	// Identifiers of the test's own: a persistent domain is the whole Mac's,
	// so the real ones would reach the installed Pane's preferences.
	@Test("an earlier Pane's data and caches move to this identifier's folders, once")
	func moveFormerData() throws {
		let root = fileManager.temporaryDirectory.appending(path: "pane-identifier-\(UUID().uuidString)")
		let former = "pane-test-former-\(UUID().uuidString)"
		let current = "pane-test-current-\(UUID().uuidString)"
		let defaults = try #require(UserDefaults(suiteName: current))
		defer {
			try? fileManager.removeItem(at: root)
			defaults.removePersistentDomain(forName: former)
			defaults.removePersistentDomain(forName: current)

			// cfprefsd leaves each emptied domain's file behind.
			let preferences = fileManager.urls(for: .libraryDirectory, in: .userDomainMask)[0].appending(path: "Preferences")

			for name in [former, current] {
				try? fileManager.removeItem(at: preferences.appending(path: "\(name).plist"))
			}
		}

		let support = root.appending(path: "Application Support")
		let caches = root.appending(path: "Caches")
		try fileManager.createDirectory(at: support.appending(path: "\(former)/Engine"), withIntermediateDirectories: true)
		try Data("{}".utf8).write(to: support.appending(path: "\(former)/profiles.json"))
		try fileManager.createDirectory(at: caches.appending(path: "\(former)/Engine"), withIntermediateDirectories: true)
		defaults.setPersistentDomain(["NSWindow Frame PaneMainWindow": "0 0 1280 800", "NSOSPLastRootDirectory": Data([1])], forName: former)

		let move = { BundleIdentifier.moveFormerData(applicationSupport: support, caches: caches, defaults: defaults, from: former, to: current) }

		#expect(move().isEmpty)
		#expect(fileManager.fileExists(atPath: support.appending(path: "\(current)/profiles.json").path))
		#expect(fileManager.fileExists(atPath: caches.appending(path: "\(current)/Engine").path))
		#expect(!fileManager.fileExists(atPath: support.appending(path: former).path))
		#expect(defaults.string(forKey: "NSWindow Frame PaneMainWindow") == "0 0 1280 800")
		#expect(defaults.persistentDomain(forName: former)?.isEmpty ?? true)

		// An earlier Pane's folder that turns up later moves nowhere: this
		// identifier's data is the one in use.
		try fileManager.createDirectory(at: support.appending(path: former), withIntermediateDirectories: true)
		#expect(move().isEmpty)
		#expect(fileManager.fileExists(atPath: support.appending(path: former).path))
	}
}

@Suite("Session caches")
struct SessionCachesTests {
	@Test("the former identifier's folders in the user's cache folder go, and nothing else there")
	func formerUserCaches() throws {
		let fileManager = FileManager.default
		let caches = fileManager.temporaryDirectory.appending(path: "pane-user-caches-\(UUID().uuidString)")
		defer { try? fileManager.removeItem(at: caches) }

		for name in ["pane-old", "pane-old.helper", "pane-new", "pane-new.helper"] {
			try fileManager.createDirectory(at: caches.appending(path: "\(name)/fsCachedData"), withIntermediateDirectories: true)
			try Data("db".utf8).write(to: caches.appending(path: "\(name)/Cache.db"))
		}

		#expect(BundleIdentifier.removeFormerUserCaches(userCaches: caches, former: "pane-old").isEmpty)
		#expect(try fileManager.contentsOfDirectory(atPath: caches.path).sorted() == ["pane-new", "pane-new.helper"])
		// Nothing left to remove the next time.
		#expect(BundleIdentifier.removeFormerUserCaches(userCaches: caches, former: "pane-old").isEmpty)
		#expect(BundleIdentifier.userCacheDirectory?.lastPathComponent == "C")
	}

	@Test("an earlier Pane's URL session caches go, under either identifier, and nothing else of its folders")
	func removed() throws {
		let fileManager = FileManager.default
		let library = fileManager.temporaryDirectory.appending(path: "pane-library-\(UUID().uuidString)")
		defer { try? fileManager.removeItem(at: library) }

		for identifier in ["pane-a", "pane-b"] {
			let caches = library.appending(path: "Caches/\(identifier)")
			try fileManager.createDirectory(at: caches.appending(path: "fsCachedData"), withIntermediateDirectories: true)
			try fileManager.createDirectory(at: caches.appending(path: "Engine"), withIntermediateDirectories: true)
			try Data("db".utf8).write(to: caches.appending(path: "Cache.db"))
			try fileManager.createDirectory(at: library.appending(path: "HTTPStorages/\(identifier)"), withIntermediateDirectories: true)
		}

		#expect(BundleIdentifier.removeSessionCaches(library: library, identifiers: ["pane-a", "pane-b"]).isEmpty)

		for identifier in ["pane-a", "pane-b"] {
			let caches = library.appending(path: "Caches/\(identifier)")
			#expect(!fileManager.fileExists(atPath: caches.appending(path: "Cache.db").path))
			#expect(!fileManager.fileExists(atPath: caches.appending(path: "fsCachedData").path))
			#expect(!fileManager.fileExists(atPath: library.appending(path: "HTTPStorages/\(identifier)").path))
			#expect(fileManager.fileExists(atPath: caches.appending(path: "Engine").path))
		}
	}
}

@Suite("DataLock")
struct DataLockTests {
	private func root() throws -> URL {
		let root = FileManager.default.temporaryDirectory.appending(path: "pane-lock-\(UUID().uuidString)")
		try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
		return root
	}

	@Test("the data's lock is one Pane's while it runs, and the next one's once it lets go")
	func held() throws {
		let root = try root()
		defer { try? FileManager.default.removeItem(at: root) }

		var first = DataLock(dataDirectory: root)
		#expect(first != nil)
		#expect(DataLock(dataDirectory: root) == nil)

		// Taken as the next Pane takes it (main.swift), waiting: a process
		// another test starts holds the parent's descriptors until it execs,
		// which under the thread sanitizer outlasted the release (CI).
		first = nil
		#expect(DataLock.wait(dataDirectory: root, timeout: 3) != nil)
	}

	@Test("a Pane started while another quits takes the lock once that one lets go")
	func waits() async throws {
		let root = try root()
		defer { try? FileManager.default.removeItem(at: root) }

		// The wait can take the lock only once the first lets go, so no clock
		// is checked: a sanitized build on a loaded CI runner took 4.4 s.
		var first = DataLock(dataDirectory: root)
		let waiting = Task.detached { DataLock.wait(dataDirectory: root, timeout: 30) }

		try await Task.sleep(for: .milliseconds(200))
		#expect(first != nil)
		first = nil

		let taken = await waiting.value
		#expect(taken != nil)
		#expect(DataLock.wait(dataDirectory: root, timeout: 0.1) == nil)
	}

	@Test("a process Pane starts does not keep the lock: the update's relaunch takes it")
	func notInherited() throws {
		let root = try root()
		defer { try? FileManager.default.removeItem(at: root) }

		var lock = DataLock(dataDirectory: root)
		let child = Process()
		child.executableURL = URL(fileURLWithPath: "/bin/sleep")
		child.arguments = ["5"]
		try child.run()
		defer { child.terminate() }

		// Waited for briefly, as in held: the child sleeps for longer, so a
		// lock it inherited would still fail this.
		#expect(lock != nil)
		lock = nil
		#expect(DataLock.wait(dataDirectory: root, timeout: 2) != nil)
	}
}
