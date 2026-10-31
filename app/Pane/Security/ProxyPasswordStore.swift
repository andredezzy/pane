import Foundation
import PaneKit
import Security
import os

// Where an earlier Pane kept the proxy passwords (PaneKit ProxyPasswordVault):
// proxy-passwords.json, sealed under the engine's "Pane Safe Storage" key (ADR
// 0095), or, before that, a Keychain item of their own, "Pane Proxy
// Passwords", whose account is the data directory's path (under the former
// identifier's folder, for a data root ADR 0114 moved). Since ADR 0113 they
// are in profiles.json, sealed with it: these are read once, when an earlier
// Pane's plain profiles.json is (ModelStore), and deleted once the profiles
// are sealed with them. Under --use-mock-keychain, as the engine's own item,
// they are a file in the data directory instead, and the login Keychain is
// never touched: scenarios run so.
struct ProxyPasswordStore: Sendable {
	private static let service = "Pane Proxy Passwords"

	// The data directory's path, and where it was before ADR 0114 moved it.
	private let accounts: [String]
	private let mock: PersistedFile<[String: String]>?
	private let sealed: PersistedFile<ProxyPasswordSeal.Sealed>

	init(dataDirectory: URL) {
		let path = dataDirectory.standardizedFileURL.path
		let former = dataDirectory.deletingLastPathComponent().appending(path: BundleIdentifier.former).standardizedFileURL.path
		accounts = dataDirectory.lastPathComponent == BundleIdentifier.pane ? [path, former] : [path]
		mock = CommandLine.arguments.contains("--use-mock-keychain")
			? PersistedFile(directory: dataDirectory, name: "mock-keychain")
			: nil
		sealed = PersistedFile(directory: dataDirectory, name: "proxy-passwords")
	}

	private var vault: ProxyPasswordVault {
		let sealed = sealed
		let item = Item(accounts: accounts)

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

	// The Keychain item an earlier Pane kept them in, under the first account
	// that has one.
	private struct Item: Sendable {
		let accounts: [String]

		func read() -> [String: String]? {
			for account in accounts {
				var result: CFTypeRef?
				let query: [CFString: Any] = [
					kSecClass: kSecClassGenericPassword,
					kSecAttrService: ProxyPasswordStore.service,
					kSecAttrAccount: account,
					kSecReturnData: true,
				]
				let status = SecItemCopyMatching(query as CFDictionary, &result)

				switch status {
				case errSecSuccess:
					return (result as? Data).flatMap { try? JSONDecoder().decode([String: String].self, from: $0) }
				case errSecItemNotFound:
					continue
				default:
					Log.security.error("Could not read the proxy passwords' item: \(status)")
					return nil
				}
			}

			return [:]
		}

		// Without asking: where macOS would ask, the item stays.
		func delete() {
			for account in accounts {
				SafeStorage.deleteKey(service: ProxyPasswordStore.service, account: account)
			}
		}
	}
}
