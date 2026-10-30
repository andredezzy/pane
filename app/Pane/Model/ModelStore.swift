import Foundation
import PaneKit
import os

// Where BrowserModel's state lives: profiles.json, sealed under the data key
// with each proxy's password in it (ADR 0113), and settings.json, in the data
// directory. The model starts with its settings; the profiles come once the
// key opens. An earlier Pane's plain profiles.json, and the proxy passwords
// it kept apart (ProxyPasswordStore), are read once then, and sealed. It writes what each save hands it on a serial
// utility queue, off the main thread (Chromium's UI thread).
@MainActor
final class ModelStore {
	private let profilesFile: SealedFile<ProfilesState>
	// An earlier Pane's, in the clear: read once, then sealed over.
	private let plainProfilesFile: PersistedFile<ProfilesState>
	private let settingsFile: PersistedFile<SettingsState>
	// Encodes and writes, in order, off the main thread.
	private let saveQueue = DispatchQueue(label: "pane.save", qos: .utility)
	private let dataDirectory: URL
	private let vault: DataVault
	// The proxy passwords an earlier Pane kept apart, read into the profiles:
	// their store goes once the profiles are sealed with them.
	private var passwordsKeptApart = false
	// A write that failed asks it to save again.
	private weak var model: BrowserModel?

	init(dataDirectory: URL, vault: DataVault) {
		self.dataDirectory = dataDirectory
		self.vault = vault
		profilesFile = SealedFile(directory: dataDirectory, name: "profiles")
		plainProfilesFile = PersistedFile(directory: dataDirectory, name: "profiles")
		settingsFile = PersistedFile(directory: dataDirectory, name: "settings")
	}

	// The model, with its settings.
	func makeModel() -> BrowserModel {
		let model = BrowserModel(
			profiles: [],
			settings: Self.load(settingsFile)?.settings ?? .defaults,
			persist: { [weak self] save in self?.write(save) }
		)
		self.model = model

		return model
	}

	// Once the vault has the key. An earlier Pane's plain profiles.json is
	// read then, with the proxy passwords it kept apart, and sealed at once:
	// on the first open, or on one after a quit cut that open short. A file
	// that neither opens nor decodes is kept aside.
	func loadProfiles() {
		guard let model, let key = vault.filesKey else {
			return
		}

		do {
			model.load(profiles: try profilesFile.load(key: key)?.profiles ?? [], changed: false)
		} catch {
			guard var profiles = (try? plainProfilesFile.load())?.profiles else {
				Log.model.error("Could not open profiles.json: \(String(describing: error), privacy: .private)")
				Self.keepAside(profilesFile.url)
				model.load(profiles: [], changed: false)
				return
			}

			// The store is read only for a proxy that signs in: macOS may ask
			// first.
			if ProxyPasswords.needsStore(profiles), let stored = ProxyPasswordStore(dataDirectory: dataDirectory).read() {
				profiles = ProxyPasswords.filled(profiles, from: stored)
				passwordsKeptApart = true
			}

			model.load(profiles: profiles, changed: true)
			saveNow()
		}
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
			Log.model.error("Could not read \(file.url.path, privacy: .private): \(String(describing: error), privacy: .private)")
			keepAside(file.url)
			return nil
		}
	}

	private static func keepAside(_ url: URL) {
		let damaged = url.appendingPathExtension("damaged")

		if !FileManager.default.fileExists(atPath: damaged.path) {
			try? FileManager.default.copyItem(at: url, to: damaged)
		}
	}

	private func write(_ save: BrowserModel.Save) {
		let profilesFile = profilesFile
		let settingsFile = settingsFile
		let dataDirectory = dataDirectory
		// Before the key opens there is nothing to save over profiles.json.
		let key = vault.filesKey
		let passwordsKeptApart = passwordsKeptApart

		saveQueue.async { [weak self] in
			do {
				if let profiles = save.profiles, let key {
					try profilesFile.save(ProfilesState(profiles: profiles), key: key)

					if passwordsKeptApart {
						ProxyPasswordStore(dataDirectory: dataDirectory).delete()
						Task { @MainActor in self?.passwordsKeptApart = false }
					}
				}
			} catch {
				Log.model.error("Could not save profiles.json: \(String(describing: error), privacy: .private)")
				Task { @MainActor in self?.model?.scheduleSave(profiles: true) }
			}

			do {
				if let settings = save.settings {
					try settingsFile.save(SettingsState(settings: settings))
				}
			} catch {
				Log.model.error("Could not save settings.json: \(String(describing: error), privacy: .private)")
				Task { @MainActor in self?.model?.scheduleSave(settings: true) }
			}
		}
	}
}
