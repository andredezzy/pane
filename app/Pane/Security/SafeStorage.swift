import Foundation
import Security
import os

// Chromium encrypts cookies with a key it keeps in the login Keychain, a
// generic password named "<app> Safe Storage" whose account is the app's
// name. The engine's is "Pane Safe Storage" (patch 0001).
enum SafeStorage {
	private static let service = "Pane Safe Storage"
	private static let account = "Pane"

	// The secret the data key is sealed under (ADR 0113): the engine's, which it
	// reads at every launch too, so macOS asks once for the two. Made here as
	// Chromium makes it when the engine has none yet and `makesOne`,
	// since a fresh install needs the data key before the engine first reads
	// its item. Never once a key is sealed: a new secret would not open it.
	// nil when the user denied it, or the Keychain failed.
	// Under --use-mock-keychain, as the engine's own, a fixed one: scenarios
	// never touch the login Keychain.
	nonisolated static func dataSecret(makesOne: Bool) -> Data? {
		if TestHooks.enabled {
			return Data("Pane mock keychain".utf8)
		}

		var result: CFTypeRef?
		let query: [CFString: Any] = [
			kSecClass: kSecClassGenericPassword,
			kSecAttrService: service,
			kSecAttrAccount: account,
			kSecReturnData: true,
		]
		let status = SecItemCopyMatching(query as CFDictionary, &result)

		switch status {
		case errSecSuccess:
			return result as? Data
		case errSecItemNotFound where makesOne:
			// 128 random bits, in base64 (keychain_password_mac.mm).
			let secret = Data(Data((0 ..< 16).map { _ in UInt8.random(in: .min ... .max) }).base64EncodedString().utf8)
			let item: [CFString: Any] = [
				kSecClass: kSecClassGenericPassword,
				kSecAttrService: service,
				kSecAttrAccount: account,
				kSecValueData: secret,
			]
			let status = SecItemAdd(item as CFDictionary, nil)

			guard status == errSecSuccess else {
				Log.security.error("Could not make the engine's key: \(status)")
				return nil
			}

			return secret
		case errSecItemNotFound:
			return nil
		default:
			Log.security.error("Could not read the engine's key: \(status)")
			return nil
		}
	}
}
