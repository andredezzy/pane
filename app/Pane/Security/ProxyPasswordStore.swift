import Foundation
import PaneKit
import Security
import os

// Where an earlier Pane kept the proxy passwords (PaneKit ProxyPasswordVault):
// proxy-passwords.json, sealed under the engine's "Pane Safe Storage" key (ADR
// 0095), or, before that, a Keychain item of their own, "Pane Proxy
// Passwords", whose account is the data directory's path. Since ADR 0113 they
// are in profiles.json, sealed with it: these are read once, when an earlier
// Pane's plain profiles.json is (ModelStore), and deleted once the profiles
// are sealed with them. Under --use-mock-keychain, as the engine's own item,
// they are a file in the data directory instead, and the login Keychain is
// never touched: scenarios run so.
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
				remove: { try? FileManager.default.removeItem(at: sealed.url) }
			),
			item: .init(read: { item.read() }, delete: { item.delete() })
		)
	}

	// nil when the store could not be read (the user denied it, say): the
	// profiles then have no password for those proxies.
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

	// Once the profiles are sealed with the passwords, and for the panic wipe.
	func delete() {
		if let mock {
			try? FileManager.default.removeItem(at: mock.url)
			return
		}

		vault.delete()
	}

	// The Keychain item an earlier Pane kept them in.
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

		// Without asking: where macOS would ask, the item stays.
		func delete() {
			SafeStorage.deleteKey(service: ProxyPasswordStore.service, account: account)
		}
	}
}
