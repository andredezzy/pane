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
				sealing: .init(load: { self.sealed }, remove: { self.sealed = nil }),
				item: .init(read: { self.item ?? [:] }, delete: { self.item = nil })
			)
		}
	}

	@Test("an earlier Pane's box opens with the engine's key")
	func sealed() throws {
		let places = Places()
		places.sealed = try ProxyPasswordSeal.seal(["a": "s3cret"], secret: Data("engine-key".utf8))

		#expect(places.vault.read() == ["a": "s3cret"])
	}

	@Test("the item an earlier Pane kept them in reads too")
	func item() {
		let places = Places()
		places.item = ["a": "from-item"]

		#expect(places.vault.read() == ["a": "from-item"])
	}

	@Test("a key the user denies, or another key, reads nothing")
	func unreadable() throws {
		let places = Places()
		places.sealed = try ProxyPasswordSeal.seal(["a": "s3cret"], secret: Data("engine-key".utf8))

		places.secret = .unavailable
		#expect(places.vault.read() == nil)

		places.secret = .key(Data("another".utf8))
		#expect(places.vault.read() == nil)
		#expect(places.sealed != nil)
	}

	@Test("once the profiles are sealed with them, neither the box nor the item stays")
	func deletes() throws {
		let places = Places()
		places.sealed = try ProxyPasswordSeal.seal(["a": "s3cret"], secret: Data("engine-key".utf8))
		places.item = ["b": "stale"]

		places.vault.delete()

		#expect(places.sealed == nil)
		#expect(places.item == nil)
	}
}
