import Foundation
import PaneKit
import Testing

@Suite("PinHash")
struct PinHashTests {
	// Made by the Electron app's bcryptjs 3.0.3 (hashSync).
	@Test("verifies the Electron app's bcryptjs hashes")
	func electronHashes() {
		#expect(PinHash.verify("1234", against: "$2b$10$6RNvGASNquZ6f5LXjFap1eMcHOZTOzRYJVRe1DrjRG2juL90W5MNO"))
		#expect(PinHash.verify("908172", against: "$2b$10$L3aErfv3P/fHFyJoj1nlbOlhXNE/XlPmAx40ZQ5PfES6lQ53p2mXC"))
		#expect(PinHash.verify("0000", against: "$2b$04$FDOi3iSOShZpLJbeU3jh0emN7Ma0VvZ./v68GcSUz/UqOZApKkSKK"))
		#expect(!PinHash.verify("1235", against: "$2b$10$6RNvGASNquZ6f5LXjFap1eMcHOZTOzRYJVRe1DrjRG2juL90W5MNO"))
		#expect(!PinHash.verify("", against: "$2b$10$6RNvGASNquZ6f5LXjFap1eMcHOZTOzRYJVRe1DrjRG2juL90W5MNO"))
	}

	// bcryptjs 2 wrote $2a$; for a PIN, 2a and 2b hash alike (they differ
	// only past 255 bytes).
	@Test("verifies an older $2a$ hash")
	func twoA() {
		#expect(PinHash.verify("1234", against: "$2a$10$6RNvGASNquZ6f5LXjFap1eMcHOZTOzRYJVRe1DrjRG2juL90W5MNO"))
	}

	@Test("rejects a malformed hash instead of crashing")
	func malformed() {
		#expect(!PinHash.verify("1234", against: ""))
		#expect(!PinHash.verify("1234", against: "not a hash"))
		#expect(!PinHash.verify("1234", against: "$2b$10$short"))
	}

	@Test("makes salted $2b$ hashes at the Electron app's cost")
	func makes() throws {
		let first = try #require(PinHash.make("4321"))
		let second = try #require(PinHash.make("4321"))

		#expect(first.hasPrefix("$2b$10$"))
		#expect(first.count == 60)
		#expect(first != second)
		#expect(PinHash.verify("4321", against: first))
		#expect(!PinHash.verify("4322", against: first))
	}
}

@Suite("SecurityState")
struct SecurityStateTests {
	@Test("reads and writes security.json as the Electron app did")
	func electronFile() throws {
		let directory = FileManager.default.temporaryDirectory.appending(path: "pane-security-\(UUID().uuidString)")
		defer { try? FileManager.default.removeItem(at: directory) }
		try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)

		let electron = #"{"state":{"pin":{"hash":"$2b$10$6RNvGASNquZ6f5LXjFap1eMcHOZTOzRYJVRe1DrjRG2juL90W5MNO","length":4},"failedAttempts":2},"version":0}"#
		try Data(electron.utf8).write(to: directory.appending(path: "security.json"))

		let file = PersistedFile<SecurityState>(directory: directory, name: "security")
		let state = try #require(try file.load())

		#expect(state.pin?.length == 4)
		#expect(state.failedAttempts == 2)
		#expect(state.remainingAttempts == 3)

		try file.save(SecurityState())
		let written = try String(contentsOf: directory.appending(path: "security.json"), encoding: .utf8)

		#expect(written == #"{"state":{"failedAttempts":0,"pin":null},"version":0}"#)
	}
}

@Suite("PinEntry")
struct PinEntryTests {
	@Test("unlock submits on the stored length, and unlocks on success")
	func unlock() {
		var entry = PinEntry(mode: .unlock, pinLength: 4)

		#expect(entry.title == nil)
		#expect(!entry.dismissable)
		#expect(entry.dots == 4)
		#expect(entry.digit("1") == .none)
		#expect(entry.digit("2") == .none)
		#expect(entry.digit("3") == .none)
		#expect(entry.digit("4") == .verify("1234"))
		#expect(entry.digit("5") == .none)
		#expect(entry.verified(true, remaining: 5) == .unlock)
	}

	@Test("a wrong PIN shakes and starts over; the last one wipes")
	func wrongPin() {
		var entry = PinEntry(mode: .unlock, pinLength: 4)

		for digit in "0000" {
			_ = entry.digit(digit)
		}

		#expect(entry.verified(false, remaining: 2) == .shake)
		entry.shook()
		#expect(entry.entered.isEmpty)
		#expect(!entry.busy)

		for digit in "0000" {
			_ = entry.digit(digit)
		}

		#expect(entry.verified(false, remaining: 0) == .wipe)
	}

	@Test("setup asks for at least four digits, then the same again")
	func setup() {
		var entry = PinEntry(mode: .setup, pinLength: 0)

		#expect(entry.title == "Enter new PIN")
		#expect(entry.dots == 4)

		for digit in "123" {
			#expect(entry.digit(digit) == .none)
		}

		#expect(!entry.canContinue)
		entry.continueWithPin()
		#expect(entry.step == .enter)

		_ = entry.digit("4")
		_ = entry.digit("5")
		#expect(entry.dots == 5)
		#expect(entry.canContinue)
		entry.continueWithPin()

		#expect(entry.step == .confirm)
		#expect(entry.title == "Confirm PIN")
		#expect(entry.dots == 5)

		for digit in "1234" {
			_ = entry.digit(digit)
		}

		#expect(entry.digit("6") == .shake)
		#expect(entry.error == "PINs don't match")
		entry.shook()

		for digit in "1234" {
			_ = entry.digit(digit)
		}

		#expect(entry.digit("5") == .setPin("12345"))
	}

	@Test("change verifies the current PIN, then takes a new one")
	func change() {
		var entry = PinEntry(mode: .change, pinLength: 4)

		#expect(entry.title == "Enter current PIN")

		for digit in "123" {
			_ = entry.digit(digit)
		}

		#expect(entry.digit("4") == .verify("1234"))
		#expect(entry.verified(true, remaining: 5) == .none)
		#expect(entry.step == .enter)
		#expect(entry.entered.isEmpty)

		for digit in "9876" {
			_ = entry.digit(digit)
		}

		entry.continueWithPin()

		for digit in "987" {
			_ = entry.digit(digit)
		}

		#expect(entry.digit("6") == .setPin("9876"))
	}

	@Test("remove verifies the current PIN; a wrong one only shakes")
	func remove() {
		var entry = PinEntry(mode: .remove, pinLength: 4)

		for digit in "1111" {
			_ = entry.digit(digit)
		}

		#expect(entry.verified(false, remaining: 0) == .shake)
		entry.shook()

		for digit in "1234" {
			_ = entry.digit(digit)
		}

		#expect(entry.verified(true, remaining: 5) == .removePin("1234"))
	}

	@Test("backspace and non-digits")
	func editing() {
		var entry = PinEntry(mode: .setup, pinLength: 0)

		#expect(entry.digit("a") == .none)
		_ = entry.digit("1")
		_ = entry.digit("2")
		entry.backspace()
		#expect(entry.entered == "1")
		entry.backspace()
		entry.backspace()
		#expect(entry.entered.isEmpty)
	}
}

@Suite("PanicWipe")
struct PanicWipeTests {
	@Test("erases the data and caches, and nothing beside them")
	func erases() throws {
		let root = FileManager.default.temporaryDirectory.appending(path: "pane-wipe-\(UUID().uuidString)")
		defer { try? FileManager.default.removeItem(at: root) }

		let data = root.appending(path: "data")
		let caches = root.appending(path: "caches")
		let outside = root.appending(path: "outside")
		let fileManager = FileManager.default

		for directory in [data.appending(path: "Engine/Profile A/Local Storage"), caches.appending(path: "Engine/Profile A/Cache"), data.appending(path: "automation"), outside] {
			try fileManager.createDirectory(at: directory, withIntermediateDirectories: true)
		}

		try Data("cookies".utf8).write(to: data.appending(path: "Engine/Profile A/Cookies"))
		try Data("storage".utf8).write(to: data.appending(path: "Engine/Profile A/Local Storage/leveldb"))
		try Data("cache".utf8).write(to: caches.appending(path: "Engine/Profile A/Cache/data_0"))
		try Data("kept".utf8).write(to: outside.appending(path: "kept.txt"))
		try Data("kept".utf8).write(to: data.appending(path: "automation/kept.json"))

		for name in ["profiles.json", "settings.json", "security.json"] {
			try Data("{}".utf8).write(to: data.appending(path: name))
		}

		// A link out of the data, as Chromium's SingletonSocket.
		try fileManager.createSymbolicLink(at: data.appending(path: "Engine/SingletonSocket"), withDestinationURL: outside)

		// A second name for the cookies' bytes, outside the data: it shows
		// they were overwritten, not only unlinked.
		let witness = outside.appending(path: "cookies-witness")
		#expect(link(data.appending(path: "Engine/Profile A/Cookies").path, witness.path) == 0)

		// Staged: out of the app's way at once.
		#expect(PanicWipe.stage(dataDirectory: data, cacheDirectory: caches).isEmpty)

		#expect(!fileManager.fileExists(atPath: data.appending(path: "Engine").path))
		#expect(!fileManager.fileExists(atPath: caches.appending(path: "Engine").path))

		for name in ["profiles.json", "settings.json", "security.json"] {
			#expect(!fileManager.fileExists(atPath: data.appending(path: name).path))
		}

		let staged = PanicWipe.staged(dataDirectory: data, cacheDirectory: caches)
		#expect(staged.map(\.passes).sorted() == [1, 3])

		// Erased.
		#expect(PanicWipe.eraseStaged(dataDirectory: data, cacheDirectory: caches).isEmpty)
		#expect(PanicWipe.staged(dataDirectory: data, cacheDirectory: caches).isEmpty)
		#expect(!fileManager.fileExists(atPath: data.appending(path: "Wiping").path))
		#expect(!fileManager.fileExists(atPath: caches.appending(path: "Wiping").path))
		#expect(try Data(contentsOf: witness) != Data("cookies".utf8))

		#expect(fileManager.fileExists(atPath: outside.appending(path: "kept.txt").path))
		#expect(fileManager.fileExists(atPath: data.appending(path: "automation/kept.json").path))
	}

	@Test("a wipe cut short is still staged, and what it cannot erase is reported")
	func cutShort() throws {
		let root = FileManager.default.temporaryDirectory.appending(path: "pane-wipe-\(UUID().uuidString)")
		let data = root.appending(path: "data")
		let fileManager = FileManager.default
		defer {
			try? fileManager.setAttributes([.posixPermissions: 0o755], ofItemAtPath: data.appending(path: "Wiping").path)
			try? fileManager.removeItem(at: root)
		}

		try fileManager.createDirectory(at: data.appending(path: "Engine/Profile A"), withIntermediateDirectories: true)
		try Data("cookies".utf8).write(to: data.appending(path: "Engine/Profile A/Cookies"))
		#expect(PanicWipe.stage(dataDirectory: data, cacheDirectory: nil).isEmpty)

		// The app quit before the erase: the next launch finds it.
		let staged = try #require(PanicWipe.staged(dataDirectory: data, cacheDirectory: nil).first)
		#expect(fileManager.fileExists(atPath: staged.url.appending(path: "Engine/Profile A/Cookies").path))

		// A folder it may not change: reported, not swallowed.
		try fileManager.setAttributes([.posixPermissions: 0o555], ofItemAtPath: staged.url.appending(path: "Engine/Profile A").path)
		let failures = PanicWipe.eraseStaged(dataDirectory: data, cacheDirectory: nil)
		try fileManager.setAttributes([.posixPermissions: 0o755], ofItemAtPath: staged.url.appending(path: "Engine/Profile A").path)

		#expect(failures.contains { $0.path.hasSuffix("Profile A/Cookies") })
		#expect(PanicWipe.eraseStaged(dataDirectory: data, cacheDirectory: nil).isEmpty)
		#expect(!fileManager.fileExists(atPath: data.appending(path: "Wiping").path))
	}

	@Test("caches live apart only under Application Support")
	func cacheDirectory() {
		let support = URL(fileURLWithPath: "/Users/someone/Library/Application Support")
		let caches = URL(fileURLWithPath: "/Users/someone/Library/Caches")

		#expect(
			PanicWipe.cacheDirectory(for: support.appending(path: "com.pane.desktop"), applicationSupport: support, caches: caches)
				== caches.appending(path: "com.pane.desktop")
		)
		#expect(PanicWipe.cacheDirectory(for: URL(fileURLWithPath: "/tmp/pane-data"), applicationSupport: support, caches: caches) == nil)
		#expect(PanicWipe.cacheDirectory(for: support, applicationSupport: support, caches: caches) == nil)
	}
}
