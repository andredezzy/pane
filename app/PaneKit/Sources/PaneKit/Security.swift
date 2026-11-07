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

// security.json's state, with the data key.
public struct SecurityState: Codable, Hashable, Sendable {
	public static let maxAttempts = 5

	public var pin: Pin?
	public var failedAttempts: Int
	// nil until the first launch that makes it.
	public var key: DataKey.Sealed?

	public init(pin: Pin? = nil, failedAttempts: Int = 0, key: DataKey.Sealed? = nil) {
		self.pin = pin
		self.failedAttempts = failedAttempts
		self.key = key
	}

	public var remainingAttempts: Int {
		max(0, Self.maxAttempts - failedAttempts)
	}
}
