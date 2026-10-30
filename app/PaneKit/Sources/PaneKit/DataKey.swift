import CommonCrypto
import CryptoKit
import Foundation

// The data key (ADR 0113): 256 random bits that every profile's data is
// encrypted under, through a key per use. It is kept only sealed, with
// AES-GCM, under a key HKDF-SHA256 derives from the engine's "Pane Safe
// Storage" secret, which the engine reads at every launch anyway. With a PIN,
// the PIN stretched with PBKDF2 is that derivation's salt, so the key opens
// only with both, and opening it is how the PIN is checked.
public enum DataKey {
	// OWASP's rounds for PBKDF2-HMAC-SHA256: 64 ms on an M5 Pro, where the
	// bcrypt it replaces (cost 10) took 40.
	public static let rounds = 600_000

	public struct Sealed: Codable, Hashable, Sendable {
		// AES-GCM's combined box (nonce, ciphertext, and tag), in base64.
		public var box: String
		// How the PIN was stretched; nil when it was sealed without one.
		public var pin: Stretch?
		// Tells another secret from a wrong PIN: without it, an engine key
		// replaced would make every PIN look wrong, and the fifth would wipe.
		public var secret: String

		public init(box: String, pin: Stretch?, secret: String) {
			self.box = box
			self.pin = pin
			self.secret = secret
		}
	}

	// The secret is not the one the key was sealed under: not the PIN's
	// fault, so no attempt counts.
	public struct OtherSecret: Error {}

	public struct Stretch: Codable, Hashable, Sendable {
		// 16 random bytes, in base64.
		public var salt: String
		public var rounds: Int
	}

	public static func make() -> SymmetricKey {
		SymmetricKey(size: .bits256)
	}

	// Each seal stretches the PIN with a salt of its own.
	public static func seal(_ key: SymmetricKey, secret: Data, pin: String?) throws -> Sealed {
		let stretch = pin.map { _ in
			Stretch(salt: Data((0 ..< 16).map { _ in UInt8.random(in: .min ... .max) }).base64EncodedString(), rounds: rounds)
		}
		let box = try AES.GCM.seal(key.withUnsafeBytes { Data($0) }, using: sealingKey(secret: secret, pin: pin, stretch: stretch))

		guard let combined = box.combined else {
			throw CryptoKitError.incorrectParameterSize
		}

		return Sealed(box: combined.base64EncodedString(), pin: stretch, secret: check(secret))
	}

	// Throws OtherSecret for another secret, and otherwise for another PIN,
	// a box changed since it was sealed, and a PIN for a key sealed without
	// one, or none for a key sealed with one.
	public static func open(_ sealed: Sealed, secret: Data, pin: String?) throws -> SymmetricKey {
		guard sealed.secret == check(secret) else {
			throw OtherSecret()
		}

		guard (sealed.pin == nil) == (pin == nil) else {
			throw CryptoKitError.authenticationFailure
		}

		guard let combined = Data(base64Encoded: sealed.box) else {
			throw CryptoKitError.incorrectParameterSize
		}

		let bytes = try AES.GCM.open(AES.GCM.SealedBox(combined: combined), using: sealingKey(secret: secret, pin: pin, stretch: sealed.pin))
		return SymmetricKey(data: bytes)
	}

	// Pane's own files, sealed in place (SealedFile).
	public static func filesKey(_ key: SymmetricKey) -> SymmetricKey {
		HKDF<SHA256>.deriveKey(inputKeyMaterial: key, info: Data("Pane files".utf8), outputByteCount: 32)
	}

	// The volume's passphrase, which hdiutil reads from standard input.
	public static func volumePassphrase(_ key: SymmetricKey) -> String {
		HKDF<SHA256>.deriveKey(inputKeyMaterial: key, info: Data("Pane data volume".utf8), outputByteCount: 32)
			.withUnsafeBytes { Data($0) }
			.base64EncodedString()
	}

	// HMAC-SHA256 of the secret, cut to 16 bytes, in base64. The secret is
	// 128 random bits: no one finds it from this.
	private static func check(_ secret: Data) -> String {
		Data(HMAC<SHA256>.authenticationCode(for: Data("Pane data key secret".utf8), using: SymmetricKey(data: secret)).prefix(16)).base64EncodedString()
	}

	private static func sealingKey(secret: Data, pin: String?, stretch: Stretch?) throws -> SymmetricKey {
		var salt = Data()

		if let pin, let stretch {
			salt = try stretched(pin, stretch)
		}

		return HKDF<SHA256>.deriveKey(inputKeyMaterial: SymmetricKey(data: secret), salt: salt, info: Data("Pane data key".utf8), outputByteCount: 32)
	}

	// PBKDF2-HMAC-SHA256 through CommonCrypto, 32 bytes.
	private static func stretched(_ pin: String, _ stretch: Stretch) throws -> Data {
		guard let salt = Data(base64Encoded: stretch.salt), let rounds = UInt32(exactly: stretch.rounds) else {
			throw CryptoKitError.incorrectParameterSize
		}

		var output = [UInt8](repeating: 0, count: 32)
		let status = CCKeyDerivationPBKDF(
			CCPBKDFAlgorithm(kCCPBKDF2), pin, pin.utf8.count, [UInt8](salt), salt.count,
			CCPseudoRandomAlgorithm(kCCPRFHmacAlgSHA256), rounds, &output, output.count
		)

		guard status == kCCSuccess else {
			throw CryptoKitError.underlyingCoreCryptoError(error: status)
		}

		return Data(output)
	}
}
