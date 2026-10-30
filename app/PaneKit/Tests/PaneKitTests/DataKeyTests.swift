import CryptoKit
import Foundation
import PaneKit
import Testing

// A key's bytes, to compare two keys.
private func bytes(_ key: SymmetricKey) -> Data {
	key.withUnsafeBytes { Data($0) }
}

@Suite("DataKey")
struct DataKeyTests {
	private let secret = Data("engine-safe-storage-secret".utf8)

	@Test("sealed without a PIN, the key opens with the secret alone")
	func withoutPin() throws {
		let key = DataKey.make()
		let sealed = try DataKey.seal(key, secret: secret, pin: nil)

		#expect(sealed.pin == nil)
		#expect(try bytes(DataKey.open(sealed, secret: secret, pin: nil)) == bytes(key))
		#expect(throws: DataKey.OtherSecret.self) { try DataKey.open(sealed, secret: Data("another".utf8), pin: nil) }
		#expect(throws: (any Error).self) { try DataKey.open(sealed, secret: secret, pin: "1234") }
	}

	@Test("sealed with a PIN, the key opens with the secret and that PIN only")
	func withPin() throws {
		let key = DataKey.make()
		let sealed = try DataKey.seal(key, secret: secret, pin: "908172")

		#expect(sealed.pin?.rounds == DataKey.rounds)
		#expect(!sealed.box.isEmpty)
		#expect(try bytes(DataKey.open(sealed, secret: secret, pin: "908172")) == bytes(key))
		#expect(throws: (any Error).self) { try DataKey.open(sealed, secret: secret, pin: "908173") }
		#expect(throws: (any Error).self) { try DataKey.open(sealed, secret: secret, pin: nil) }
		// Another secret is told apart from a wrong PIN, which a wipe counts.
		#expect(throws: DataKey.OtherSecret.self) { try DataKey.open(sealed, secret: Data("another".utf8), pin: "908172") }
		#expect {
			try DataKey.open(sealed, secret: secret, pin: "908173")
		} throws: { !($0 is DataKey.OtherSecret) }
	}

	@Test("a new PIN reseals the same key, which the old PIN no longer opens")
	func reseal() throws {
		let key = DataKey.make()
		let first = try DataKey.seal(key, secret: secret, pin: "1234")
		let second = try DataKey.seal(DataKey.open(first, secret: secret, pin: "1234"), secret: secret, pin: "5678")

		#expect(second.pin?.salt != first.pin?.salt)
		#expect(try bytes(DataKey.open(second, secret: secret, pin: "5678")) == bytes(key))
		#expect(throws: (any Error).self) { try DataKey.open(second, secret: secret, pin: "1234") }
	}

	@Test("a box changed since it was sealed does not open")
	func tampered() throws {
		let sealed = try DataKey.seal(DataKey.make(), secret: secret, pin: nil)
		var box = try #require(Data(base64Encoded: sealed.box))
		box[box.count - 1] ^= 0x01

		#expect(throws: (any Error).self) { try DataKey.open(.init(box: box.base64EncodedString(), pin: nil, secret: sealed.secret), secret: secret, pin: nil) }
	}

	@Test("each use has a key of its own, the same for the same data key")
	func derived() throws {
		let key = DataKey.make()
		let passphrase = DataKey.volumePassphrase(key)

		#expect(bytes(DataKey.filesKey(key)) == bytes(DataKey.filesKey(key)))
		#expect(bytes(DataKey.filesKey(key)) != bytes(key))
		#expect(passphrase == DataKey.volumePassphrase(key))
		#expect(passphrase != DataKey.volumePassphrase(DataKey.make()))
		#expect(try #require(Data(base64Encoded: passphrase)) != bytes(DataKey.filesKey(key)))
	}
}

@Suite("SealedFile")
struct SealedFileTests {
	private let tabs = ["inbox": "https://mail.example.com/inbox"]

	private func directory() throws -> URL {
		let directory = FileManager.default.temporaryDirectory.appending(path: "pane-sealed-\(UUID().uuidString)")
		try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
		return directory
	}

	@Test("writes nothing in the clear, and reads back with its key only")
	func roundTrip() throws {
		let directory = try directory()
		defer { try? FileManager.default.removeItem(at: directory) }
		let file = SealedFile<[String: String]>(directory: directory, name: "profiles")
		let key = DataKey.filesKey(DataKey.make())

		try file.save(tabs, key: key)

		#expect(try !String(contentsOf: file.url, encoding: .utf8).contains("mail.example.com"))
		#expect(try file.load(key: key) == tabs)
		#expect(throws: (any Error).self) { try file.load(key: DataKey.filesKey(DataKey.make())) }
	}

	@Test("a box moved to another file does not open")
	func moved() throws {
		let directory = try directory()
		defer { try? FileManager.default.removeItem(at: directory) }
		let key = DataKey.filesKey(DataKey.make())
		let profiles = SealedFile<[String: String]>(directory: directory, name: "profiles")
		let deletions = SealedFile<[String: String]>(directory: directory, name: "deletions")

		try profiles.save(tabs, key: key)
		try FileManager.default.copyItem(at: profiles.url, to: deletions.url)

		#expect(throws: (any Error).self) { try deletions.load(key: key) }
	}

	@Test("a plain file, as PersistedFile writes it, does not read")
	func plain() throws {
		let directory = try directory()
		defer { try? FileManager.default.removeItem(at: directory) }

		try PersistedFile<[String: String]>(directory: directory, name: "profiles").save(tabs)

		#expect(throws: (any Error).self) {
			try SealedFile<[String: String]>(directory: directory, name: "profiles").load(key: DataKey.filesKey(DataKey.make()))
		}
	}

	@Test("a file not written yet reads as nil")
	func missing() throws {
		let directory = try directory()
		defer { try? FileManager.default.removeItem(at: directory) }

		#expect(try SealedFile<[String: String]>(directory: directory, name: "profiles").load(key: DataKey.filesKey(DataKey.make())) == nil)
	}
}
