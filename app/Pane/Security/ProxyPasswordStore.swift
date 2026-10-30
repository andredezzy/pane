import Foundation
import PaneKit
import Security
import os

// Where proxy passwords live (PaneKit ProxyPasswords): sealed in the data
// directory, proxy-passwords.json, under the engine's "Pane Safe Storage" key
// (PaneKit ProxyPasswordVault, ADR 0095). The engine reads that item at every
// launch anyway, so after an update of an ad-hoc signed build macOS asks once,
// for it, and the passwords ask nothing more. An earlier Pane kept them in a
// Keychain item of their own, "Pane Proxy Passwords", whose account is the
// data directory's path: it is read once more, sealed, and deleted. Under
// --use-mock-keychain, as the engine's own item, they are a file in the data
// directory instead, and the login Keychain is never touched: scenarios run
// so.
struct ProxyPasswordStore: Sendable {
	private static let service = "Pane Proxy Passwords"

	private let account: String
	private let mock: PersistedFile<[String: String]>?
	private let sealed: PersistedFile<ProxyPasswordSeal.Sealed>

	init(dataDirectory: URL) {
		account = dataDirectory.standardizedFileURL.path
		mock = CommandLine.arguments.contains("--use-mock-keychain")
			? PersistedFile(directory: dataDirectory, name: "mock-keychain")
			: nil
		sealed = PersistedFile(directory: dataDirectory, name: "proxy-passwords")
	}

	private var vault: ProxyPasswordVault {
		let sealed = sealed
		let item = Item(account: account)

		return ProxyPasswordVault(
			secret: { SafeStorage.engineSecret() },
			sealing: .init(
				load: { try sealed.load() },
				save: { try sealed.save($0) },
				remove: { try? FileManager.default.removeItem(at: sealed.url) }
			),
			item: .init(read: { item.read() }, write: { item.write($0) }, delete: { item.delete() })
		)
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

		guard let passwords = vault.read() else {
			Log.security.error("Could not read the proxy passwords")
			return nil
		}

		return passwords
	}

	// Whether the store now holds `passwords`.
	func write(_ passwords: [String: String]) -> Bool {
		if let mock {
			guard !passwords.isEmpty else {
				try? FileManager.default.removeItem(at: mock.url)
				return true
			}

			return (try? mock.save(passwords)) != nil
		}

		guard vault.write(passwords) else {
			Log.security.error("Could not save the proxy passwords")
			return false
		}

		return true
	}

	// For the panic wipe, and once no proxy has a password.
	func delete() {
		if let mock {
			try? FileManager.default.removeItem(at: mock.url)
			return
		}

		vault.delete()
	}

	// The Keychain item an earlier Pane kept them in, and the one they go to
	// before the engine has a key of its own.
	private struct Item: Sendable {
		let account: String

		private var query: [CFString: Any] {
			[
				kSecClass: kSecClassGenericPassword,
				kSecAttrService: ProxyPasswordStore.service,
				kSecAttrAccount: account,
			]
		}

		func read() -> [String: String]? {
			var result: CFTypeRef?
			let status = SecItemCopyMatching(query.merging([kSecReturnData: true]) { $1 } as CFDictionary, &result)

			switch status {
			case errSecSuccess:
				return (result as? Data).flatMap { try? JSONDecoder().decode([String: String].self, from: $0) }
			case errSecItemNotFound:
				return [:]
			default:
				Log.security.error("Could not read the proxy passwords' item: \(status)")
				return nil
			}
		}

		func write(_ passwords: [String: String]) -> Bool {
			guard let data = try? JSONEncoder().encode(passwords) else {
				return false
			}

			var status = SecItemUpdate(query as CFDictionary, [kSecValueData: data] as CFDictionary)

			if status == errSecItemNotFound {
				status = SecItemAdd(query.merging([kSecValueData: data]) { $1 } as CFDictionary, nil)
			}

			return status == errSecSuccess
		}

		// Without asking: where macOS would ask, the item stays.
		func delete() {
			SafeStorage.deleteKey(service: ProxyPasswordStore.service, account: account)
		}
	}
}
