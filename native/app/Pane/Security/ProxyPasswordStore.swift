import Foundation
import PaneKit
import Security

// Where proxy passwords live (PaneKit ProxyPasswords): one generic password
// in the login Keychain, "Pane Proxy Passwords", whose secret is the JSON of
// profile id → password, so macOS asks at most once (after an update of an
// ad-hoc signed build). Its account is the data directory's path: a Pane on
// another data directory (PANE_DATA_DIR) keeps its own. Under
// --use-mock-keychain, as the engine's own item, it is a file in the data
// directory instead, and the login Keychain is never touched: scenarios run
// so.
struct ProxyPasswordStore: Sendable {
	private static let service = "Pane Proxy Passwords"

	private let account: String
	private let mock: PersistedFile<[String: String]>?

	init(dataDirectory: URL) {
		account = dataDirectory.standardizedFileURL.path
		mock = CommandLine.arguments.contains("--use-mock-keychain")
			? PersistedFile(directory: dataDirectory, name: "mock-keychain")
			: nil
	}

	private var item: [CFString: Any] {
		[
			kSecClass: kSecClassGenericPassword,
			kSecAttrService: Self.service,
			kSecAttrAccount: account,
		]
	}

	// nil when the store could not be read (the user denied it, say): the
	// passwords then stay where they are.
	func read() -> [String: String]? {
		if let mock {
			do {
				return try mock.load() ?? [:]
			} catch {
				return nil
			}
		}

		var result: CFTypeRef?
		let status = SecItemCopyMatching(item.merging([kSecReturnData: true]) { $1 } as CFDictionary, &result)

		switch status {
		case errSecSuccess:
			return (result as? Data).flatMap { try? JSONDecoder().decode([String: String].self, from: $0) }
		case errSecItemNotFound:
			return [:]
		default:
			NSLog("Pane: could not read the proxy passwords: %d", status)
			return nil
		}
	}

	// Whether the store now holds `passwords`.
	func write(_ passwords: [String: String]) -> Bool {
		guard !passwords.isEmpty else {
			delete()
			return true
		}

		if let mock {
			return (try? mock.save(passwords)) != nil
		}

		guard let data = try? JSONEncoder().encode(passwords) else {
			return false
		}

		var status = SecItemUpdate(item as CFDictionary, [kSecValueData: data] as CFDictionary)

		if status == errSecItemNotFound {
			status = SecItemAdd(item.merging([kSecValueData: data]) { $1 } as CFDictionary, nil)
		}

		if status != errSecSuccess {
			NSLog("Pane: could not save the proxy passwords: %d", status)
		}

		return status == errSecSuccess
	}

	// For the panic wipe, and once no proxy has a password.
	func delete() {
		if let mock {
			try? FileManager.default.removeItem(at: mock.url)
			return
		}

		SecItemDelete(item as CFDictionary)
	}
}
