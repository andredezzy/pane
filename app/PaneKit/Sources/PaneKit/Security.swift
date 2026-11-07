import Foundation

// The app lock (apps/veil security-store.ts and trpc/routers/security.ts): a
// PIN, the failed attempts that lead to the panic wipe, and the data key,
// sealed under the engine's secret and the PIN (ADR 0113), in security.json.

public struct Pin: Codable, Hashable, Sendable {
	// The digits, so the lock screen draws that many dots and submits on the
	// last one.
	public var length: Int

	public init(length: Int) {
		self.length = length
	}
}

// security.json's state, as the Electron app persisted it, with the data key.
public struct SecurityState: Codable, Hashable, Sendable {
	public static let maxAttempts = 5

	public var pin: Pin?
	public var failedAttempts: Int
	// nil until the first launch that makes it.
	public var key: DataKey.Sealed?

	private enum CodingKeys: String, CodingKey {
		case pin, failedAttempts, key
	}

	public init(pin: Pin? = nil, failedAttempts: Int = 0, key: DataKey.Sealed? = nil) {
		self.pin = pin
		self.failedAttempts = failedAttempts
		self.key = key
	}

	public init(from decoder: Decoder) throws {
		let container = try decoder.container(keyedBy: CodingKeys.self)
		pin = try container.decodeIfPresent(Pin.self, forKey: .pin)
		failedAttempts = try container.decodeIfPresent(Int.self, forKey: .failedAttempts) ?? 0
		key = try container.decodeIfPresent(DataKey.Sealed.self, forKey: .key)
	}

	// An explicit null, as the Electron app writes it.
	public func encode(to encoder: Encoder) throws {
		var container = encoder.container(keyedBy: CodingKeys.self)
		try container.encode(pin, forKey: .pin)
		try container.encode(failedAttempts, forKey: .failedAttempts)
		try container.encodeIfPresent(key, forKey: .key)
	}

	public var remainingAttempts: Int {
		max(0, Self.maxAttempts - failedAttempts)
	}
}
