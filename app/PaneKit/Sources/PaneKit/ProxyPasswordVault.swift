import CryptoKit
import Foundation

// Proxy passwords as an earlier Pane sealed them under the engine's key (ADR
// 0095): AES-GCM, with a key HKDF-SHA256 derives from the "Pane Safe Storage"
// secret. Read once since ADR 0113, which seals them in profiles.json.
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

// Where an earlier Pane kept the proxy passwords, apart from profiles.json:
// sealed under the engine's key (ADR 0095), or, before that, in a Keychain
// item of their own ("Pane Proxy Passwords"). Since ADR 0113 they are in
// profiles.json, sealed with it: these are read once, when an earlier Pane's
// plain profiles.json is, then deleted. The app gives it the Keychain and the
// file (app/Pane/Security/ProxyPasswordStore.swift).
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
		public var remove: @Sendable () -> Void

		public init(load: @escaping @Sendable () throws -> ProxyPasswordSeal.Sealed?, remove: @escaping @Sendable () -> Void) {
			self.load = load
			self.remove = remove
		}
	}

	// The item of their own: nil from `read` when it could not be read.
	public struct Item: Sendable {
		public var read: @Sendable () -> [String: String]?
		public var delete: @Sendable () -> Void

		public init(read: @escaping @Sendable () -> [String: String]?, delete: @escaping @Sendable () -> Void) {
			self.read = read
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
	// say).
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

		return item.read()
	}

	// Once the profiles are sealed with them, and for the panic wipe.
	public func delete() {
		sealing.remove()
		item.delete()
	}
}
