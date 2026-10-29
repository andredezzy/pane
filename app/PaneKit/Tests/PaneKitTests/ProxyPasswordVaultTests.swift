import Foundation
import PaneKit
import Testing

@Suite("ProxyPasswordSeal")
struct ProxyPasswordSealTests {
	private let secret = Data("engine-safe-storage-secret".utf8)

	@Test("seals the passwords, which open with the same secret alone")
	func roundTrip() throws {
		let passwords = ["a": "s3cret", "b": "hunter2"]
		let sealed = try ProxyPasswordSeal.seal(passwords, secret: secret)

		#expect(!sealed.box.contains("s3cret"))
		#expect(try ProxyPasswordSeal.open(sealed, secret: secret) == passwords)
		#expect(throws: (any Error).self) { try ProxyPasswordSeal.open(sealed, secret: Data("another".utf8)) }
	}

	@Test("a box changed since it was sealed does not open")
	func tampered() throws {
		let sealed = try ProxyPasswordSeal.seal(["a": "s3cret"], secret: secret)
		var bytes = try #require(Data(base64Encoded: sealed.box))
		bytes[bytes.count - 1] ^= 0x01

		#expect(throws: (any Error).self) { try ProxyPasswordSeal.open(.init(box: bytes.base64EncodedString()), secret: secret) }
	}
}

@Suite("ProxyPasswordVault")
struct ProxyPasswordVaultTests {
	// The Keychain and the data directory, in memory.
	private final class Places: @unchecked Sendable {
		private let lock = NSLock()
		private var _secret: ProxyPasswordVault.Secret = .key(Data("engine-key".utf8))
		private var _sealed: ProxyPasswordSeal.Sealed?
		private var _item: [String: String]?

		var secret: ProxyPasswordVault.Secret {
			get { lock.withLock { _secret } }
			set { lock.withLock { _secret = newValue } }
		}

		var sealed: ProxyPasswordSeal.Sealed? {
			get { lock.withLock { _sealed } }
			set { lock.withLock { _sealed = newValue } }
		}

		var item: [String: String]? {
			get { lock.withLock { _item } }
			set { lock.withLock { _item = newValue } }
		}

		var vault: ProxyPasswordVault {
			ProxyPasswordVault(
				secret: { self.secret },
				sealing: .init(load: { self.sealed }, save: { self.sealed = $0 }, remove: { self.sealed = nil }),
				item: .init(read: { self.item ?? [:] }, write: { self.item = $0; return true }, delete: { self.item = nil })
			)
		}
	}

	@Test("writes sealed under the engine's key, and reads it back")
	func sealed() throws {
		let places = Places()

		#expect(places.vault.write(["a": "s3cret"]))
		#expect(places.sealed != nil)
		#expect(places.item == nil)
		#expect(places.vault.read() == ["a": "s3cret"])
	}

	@Test("an earlier Pane's item is read once more, sealed, and deleted")
	func migrates() {
		let places = Places()
		places.item = ["a": "from-item"]

		#expect(places.vault.read() == ["a": "from-item"])
		#expect(places.sealed != nil)
		#expect(places.item == nil)
		#expect(places.vault.read() == ["a": "from-item"])
	}

	@Test("before the engine has a key, the item keeps the passwords")
	func beforeTheEngine() {
		let places = Places()
		places.secret = .missing

		#expect(places.vault.write(["a": "s3cret"]))
		#expect(places.sealed == nil)
		#expect(places.item == ["a": "s3cret"])
		#expect(places.vault.read() == ["a": "s3cret"])

		// A later launch, once the engine has its key, seals them.
		places.secret = .key(Data("engine-key".utf8))
		#expect(places.vault.read() == ["a": "s3cret"])
		#expect(places.sealed != nil)
		#expect(places.item == nil)
	}

	@Test("a key the user denies leaves the passwords where they are")
	func denied() {
		let places = Places()
		#expect(places.vault.write(["a": "s3cret"]))
		places.secret = .unavailable

		#expect(places.vault.read() == nil)
		#expect(!places.vault.write(["a": "changed"]))
		#expect(places.sealed != nil)

		// An earlier Pane's item still reads, unsealed.
		let earlier = Places()
		earlier.item = ["a": "from-item"]
		earlier.secret = .unavailable
		#expect(earlier.vault.read() == ["a": "from-item"])
		#expect(earlier.item == ["a": "from-item"])
	}

	@Test("another key's box reads as nothing, and stays for its key")
	func anotherKey() {
		let places = Places()
		#expect(places.vault.write(["a": "s3cret"]))
		places.secret = .key(Data("another".utf8))

		#expect(places.vault.read() == nil)
		#expect(places.sealed != nil)
	}

	@Test("no passwords, or the wipe, leave neither the box nor the item")
	func deletes() {
		let places = Places()
		#expect(places.vault.write(["a": "s3cret"]))
		places.item = ["b": "stale"]

		#expect(places.vault.write([:]))
		#expect(places.sealed == nil)
		#expect(places.item == nil)

		#expect(places.vault.write(["a": "s3cret"]))
		places.vault.delete()
		#expect(places.sealed == nil)
	}
}
