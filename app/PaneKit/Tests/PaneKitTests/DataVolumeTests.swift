import Foundation
import PaneKit
import Testing

// The volume's links and what it refuses, on a plain folder standing in for the
// attached volume, and its image where a test makes one.
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

	// What 0.13.1 and before wrote, never moved into a volume: a link there would
	// delete it.
	@Test("a plain folder in a profile's place is refused, and stays")
	func plainFolderRefused() throws {
		let root = try root()
		defer { try? fileManager.removeItem(at: root) }
		let data = root.appending(path: "data")
		let volume = DataVolume(dataDirectory: data, cacheDirectory: nil)
		let cookies = data.appending(path: "Engine/p/Cookies")

		try fileManager.createDirectory(at: cookies.deletingLastPathComponent(), withIntermediateDirectories: true)
		try Data("cookie".utf8).write(to: cookies)

		#expect(throws: DataVolume.BeforeTheFloor.self) {
			try volume.prepare(profile: "p")
		}

		#expect(try Data(contentsOf: cookies) == Data("cookie".utf8))
		#expect(link(data.appending(path: "Engine/p")) == nil)
		#expect(!exists(volume.folder(profile: "p").appending(path: ".files-encrypted")))
	}

	@Test("a plain folder of caches in a profile's place is refused too, and stays")
	func plainCachesRefused() throws {
		let root = try root()
		defer { try? fileManager.removeItem(at: root) }
		let caches = root.appending(path: "caches")
		let volume = DataVolume(dataDirectory: root.appending(path: "data"), cacheDirectory: caches)
		let entry = caches.appending(path: "Engine/p/Cache/entry")

		try fileManager.createDirectory(at: entry.deletingLastPathComponent(), withIntermediateDirectories: true)
		try Data("page".utf8).write(to: entry)

		#expect(throws: DataVolume.BeforeTheFloor.self) {
			try volume.prepare(profile: "p")
		}

		#expect(try Data(contentsOf: entry) == Data("page".utf8))
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
