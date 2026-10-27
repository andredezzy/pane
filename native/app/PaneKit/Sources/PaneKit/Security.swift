import CBcrypt
import Foundation

// The app lock (apps/veil security-store.ts and trpc/routers/security.ts): a
// PIN, kept as a bcrypt hash in security.json, and the failed attempts that
// lead to the panic wipe.

public struct Pin: Codable, Hashable, Sendable {
	public var hash: String
	// The digits, so the lock screen draws that many dots and submits on the
	// last one.
	public var length: Int

	public init(hash: String, length: Int) {
		self.hash = hash
		self.length = length
	}
}

// security.json's state, as the Electron app persisted it.
public struct SecurityState: Codable, Hashable, Sendable {
	public static let maxAttempts = 5

	public var pin: Pin?
	public var failedAttempts: Int

	private enum CodingKeys: String, CodingKey {
		case pin, failedAttempts
	}

	public init(pin: Pin? = nil, failedAttempts: Int = 0) {
		self.pin = pin
		self.failedAttempts = failedAttempts
	}

	public init(from decoder: Decoder) throws {
		let container = try decoder.container(keyedBy: CodingKeys.self)
		pin = try container.decodeIfPresent(Pin.self, forKey: .pin)
		failedAttempts = try container.decodeIfPresent(Int.self, forKey: .failedAttempts) ?? 0
	}

	// An explicit null, as the Electron app writes it.
	public func encode(to encoder: Encoder) throws {
		var container = encoder.container(keyedBy: CodingKeys.self)
		try container.encode(pin, forKey: .pin)
		try container.encode(failedAttempts, forKey: .failedAttempts)
	}

	public var remainingAttempts: Int {
		max(0, Self.maxAttempts - failedAttempts)
	}
}

// bcrypt through OpenBSD's implementation (CBcrypt): it reads the Electron
// app's bcryptjs hashes ($2a$ and $2b$) and writes $2b$ ones, at the same
// cost.
public enum PinHash {
	// The Electron app's SALT_ROUNDS.
	public static let rounds: Int32 = 10

	public static func make(_ pin: String, rounds: Int32 = rounds) -> String? {
		var hash = [CChar](repeating: 0, count: 61)

		guard bcrypt_newhash(pin, rounds, &hash, hash.count) == 0 else {
			return nil
		}

		return String(decoding: hash.prefix { $0 != 0 }.map { UInt8(bitPattern: $0) }, as: UTF8.self)
	}

	public static func verify(_ pin: String, against hash: String) -> Bool {
		bcrypt_checkpass(pin, hash) == 0
	}
}
