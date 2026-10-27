import Foundation
import Security

// Chromium encrypts cookies with a key it keeps in the login Keychain, a
// generic password named "<app> Safe Storage" whose account is the app's
// name. The engine's is "Pane Safe Storage" (patch 0001); the Electron app's
// is named after its package ("@pane/veil Safe Storage"). A migration hands
// the Electron app's key to the engine's item, so the cookies it copied still
// decrypt.
enum SafeStorage {
	static let service = "Pane Safe Storage"
	static let account = "Pane"

	// macOS asks the user whether Pane may read the Electron app's item.
	// Without its key, the migrated sites are signed out; nothing else is
	// lost.
	@discardableResult
	static func adoptKey(service electronService: String, account electronAccount: String) -> OSStatus {
		var result: CFTypeRef?
		let query: [CFString: Any] = [
			kSecClass: kSecClassGenericPassword,
			kSecAttrService: electronService,
			kSecAttrAccount: electronAccount,
			kSecReturnData: true,
		]
		let status = SecItemCopyMatching(query as CFDictionary, &result)

		guard status == errSecSuccess, let key = result as? Data else {
			return status
		}

		// Nothing is encrypted with an earlier key of Pane's: the migration
		// runs only before the native app has data.
		let item: [CFString: Any] = [
			kSecClass: kSecClassGenericPassword,
			kSecAttrService: service,
			kSecAttrAccount: account,
		]
		SecItemDelete(item as CFDictionary)

		return SecItemAdd(item.merging([kSecValueData: key]) { $1 } as CFDictionary, nil)
	}
}
