import CryptoKit
import Foundation

// Proxy passwords sealed under the engine's key (ADR 0095): AES-GCM, with a
// key HKDF-SHA256 derives from the "Pane Safe Storage" secret. The engine
// reads that Keychain item at every launch, so after an update of an ad-hoc
// signed build macOS asks for it once, and the passwords ask nothing more.
public enum ProxyPasswordSeal {
	// AES-GCM's combined box (nonce, ciphertext, and tag), in base64.
	public struct Sealed: Codable, Hashable, Sendable {
		public var box: String

		public init(box: String) {
			self.box = box
		}
	}

	public static func seal(_ passwords: [String: String], secret: Data) throws -> Sealed {
		let box = try AES.GCM.seal(try JSONEncoder().encode(passwords), using: key(secret))

		guard let combined = box.combined else {
			throw CryptoKitError.incorrectParameterSize
		}

		return Sealed(box: combined.base64EncodedString())
	}

	// Throws for another secret's box, or a box changed since it was sealed.
	public static func open(_ sealed: Sealed, secret: Data) throws -> [String: String] {
		guard let combined = Data(base64Encoded: sealed.box) else {
			throw CryptoKitError.incorrectParameterSize
		}

		let data = try AES.GCM.open(AES.GCM.SealedBox(combined: combined), using: key(secret))
		return try JSONDecoder().decode([String: String].self, from: data)
	}

	private static func key(_ secret: Data) -> SymmetricKey {
		HKDF<SHA256>.deriveKey(inputKeyMaterial: SymmetricKey(data: secret), info: Data("Pane proxy passwords".utf8), outputByteCount: 32)
	}
}

// Where the proxy passwords are read from and written to: sealed in the data
// directory under the engine's key, or, where an earlier Pane kept them, in a
// Keychain item of their own ("Pane Proxy Passwords"), which is read once
// more, sealed, and deleted. The app gives it the Keychain and the file
// (app/Pane/Security/ProxyPasswordStore.swift).
public struct ProxyPasswordVault: Sendable {
	// The engine's key, as the Keychain gives it.
	public enum Secret: Sendable {
		case key(Data)
		// The engine has made none yet: a fresh install, before its first
		// launch is far enough along.
		case missing
		// The user denied it, or the Keychain failed.
		case unavailable
	}

	public struct Sealing: Sendable {
		public var load: @Sendable () throws -> ProxyPasswordSeal.Sealed?
		public var save: @Sendable (ProxyPasswordSeal.Sealed) throws -> Void
		public var remove: @Sendable () -> Void

		public init(load: @escaping @Sendable () throws -> ProxyPasswordSeal.Sealed?, save: @escaping @Sendable (ProxyPasswordSeal.Sealed) throws -> Void, remove: @escaping @Sendable () -> Void) {
			self.load = load
			self.save = save
			self.remove = remove
		}
	}

	// The item of their own: nil from `read` when it could not be read.
	public struct Item: Sendable {
		public var read: @Sendable () -> [String: String]?
		public var write: @Sendable ([String: String]) -> Bool
		public var delete: @Sendable () -> Void

		public init(read: @escaping @Sendable () -> [String: String]?, write: @escaping @Sendable ([String: String]) -> Bool, delete: @escaping @Sendable () -> Void) {
			self.read = read
			self.write = write
			self.delete = delete
		}
	}

	private let secret: @Sendable () -> Secret
	private let sealing: Sealing
	private let item: Item

	public init(secret: @escaping @Sendable () -> Secret, sealing: Sealing, item: Item) {
		self.secret = secret
		self.sealing = sealing
		self.item = item
	}

	// nil when the passwords could not be read (the user denied the key,
	// say): they then stay where they are.
	public func read() -> [String: String]? {
		let sealed: ProxyPasswordSeal.Sealed?

		do {
			sealed = try sealing.load()
		} catch {
			return nil
		}

		if let sealed {
			guard case let .key(key) = secret() else {
				return nil
			}

			return try? ProxyPasswordSeal.open(sealed, secret: key)
		}

		// An earlier Pane's item, read once more, and sealed in its place
		// when the engine's key can seal it.
		guard let passwords = item.read() else {
			return nil
		}

		if !passwords.isEmpty, case let .key(key) = secret(), let box = try? ProxyPasswordSeal.seal(passwords, secret: key), (try? sealing.save(box)) != nil {
			item.delete()
		}

		return passwords
	}

	// Whether the passwords are now kept. Before the engine has a key, they
	// go to the item of their own, sealed at a later launch's read.
	public func write(_ passwords: [String: String]) -> Bool {
		guard !passwords.isEmpty else {
			delete()
			return true
		}

		switch secret() {
		case let .key(key):
			guard let box = try? ProxyPasswordSeal.seal(passwords, secret: key), (try? sealing.save(box)) != nil else {
				return false
			}

			item.delete()
			return true
		case .missing:
			return item.write(passwords)
		case .unavailable:
			return false
		}
	}

	// For the panic wipe, and once no proxy has a password.
	public func delete() {
		sealing.remove()
		item.delete()
	}
}
