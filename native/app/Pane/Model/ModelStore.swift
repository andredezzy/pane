import Foundation
import PaneKit

// Where BrowserModel's state lives: profiles.json and settings.json in the
// data directory, and the proxy passwords in the Keychain (PaneKit
// ProxyPasswords). It reads them at launch, and writes what each save hands
// it on a serial utility queue, off the main thread (Chromium's UI thread).
@MainActor
final class ModelStore {
	private let profilesFile: PersistedFile<ProfilesState>
	private let settingsFile: PersistedFile<SettingsState>
	// Encodes and writes, in order, off the main thread.
	private let saveQueue = DispatchQueue(label: "pane.save", qos: .utility)
	private let passwordStore: ProxyPasswordStore
	// What the store holds, as last read or written. nil when it could not
	// be read or written this session: the passwords then stay in
	// profiles.json, as before, rather than be lost.
	private var storedPasswords: [String: String]?
	// A write that failed asks it to save again.
	private weak var model: BrowserModel?

	init(dataDirectory: URL) {
		profilesFile = PersistedFile(directory: dataDirectory, name: "profiles")
		settingsFile = PersistedFile(directory: dataDirectory, name: "settings")
		passwordStore = ProxyPasswordStore(dataDirectory: dataDirectory)
	}

	// The model, as the files and the Keychain left it.
	func makeModel() -> BrowserModel {
		let loaded = Self.load(profilesFile)?.profiles ?? []
		var profiles = loaded

		// The store is read only for a proxy that signs in: macOS may ask
		// first.
		if ProxyPasswords.needsStore(loaded) {
			storedPasswords = passwordStore.read()
			profiles = ProxyPasswords.filled(loaded, from: storedPasswords ?? [:])
		} else {
			storedPasswords = [:]
		}

		let model = BrowserModel(
			profiles: profiles,
			settings: Self.load(settingsFile)?.settings ?? .defaults,
			persist: { [weak self] save in self?.write(save) }
		)
		self.model = model

		// A password the file still holds moves to the store.
		if storedPasswords != nil, loaded.contains(where: { $0.proxy?.password != nil }) {
			model.scheduleSave(profiles: true)
		}

		return model
	}

	// Writes what changed and waits for it: at quit, and for scenarios that
	// read the files.
	func saveNow() {
		model?.saveNow()
		saveQueue.sync {}
	}

	// A file that does not decode is kept aside (<name>.json.damaged) before
	// the first save writes over it.
	private static func load<State>(_ file: PersistedFile<State>) -> State? {
		do {
			return try file.load()
		} catch {
			NSLog("Pane: could not read %@: %@", file.url.path, String(describing: error))

			let damaged = file.url.appendingPathExtension("damaged")

			if !FileManager.default.fileExists(atPath: damaged.path) {
				try? FileManager.default.copyItem(at: file.url, to: damaged)
			}

			return nil
		}
	}

	private func write(_ save: BrowserModel.Save) {
		let profilesFile = profilesFile
		let settingsFile = settingsFile
		let passwordStore = passwordStore
		let storeAvailable = storedPasswords != nil
		// The store is written only when a password changed.
		let passwords = save.profiles.map(ProxyPasswords.of).flatMap { $0 == storedPasswords || !storeAvailable ? nil : $0 }

		if let passwords {
			storedPasswords = passwords
		}

		saveQueue.async { [weak self] in
			var stripsPasswords = storeAvailable

			// The store first: a password leaves profiles.json only once the
			// store holds it.
			if let passwords, !passwordStore.write(passwords) {
				stripsPasswords = false
				Task { @MainActor in self?.storedPasswords = nil }
			}

			do {
				if let profiles = save.profiles {
					try profilesFile.save(ProfilesState(profiles: stripsPasswords ? ProxyPasswords.stripped(profiles) : profiles))
				}
			} catch {
				NSLog("Pane: could not save profiles.json: %@", String(describing: error))
				Task { @MainActor in self?.model?.scheduleSave(profiles: true) }
			}

			do {
				if let settings = save.settings {
					try settingsFile.save(SettingsState(settings: settings))
				}
			} catch {
				NSLog("Pane: could not save settings.json: %@", String(describing: error))
				Task { @MainActor in self?.model?.scheduleSave(settings: true) }
			}
		}
	}
}
