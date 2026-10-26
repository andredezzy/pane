import Foundation
import PaneKit

// Everything the app shows: profiles and their tabs (persisted), settings
// (persisted), and the session's selection, expansion, and page state.
// Views read it; BrowserController changes it as the engine reports.
@MainActor
@Observable
final class BrowserModel {
	enum Page {
		case browser
		case settings
	}

	private(set) var profiles: [BrowserProfile]
	var settings: AppSettings {
		didSet { scheduleSave() }
	}

	private(set) var history = TabHistory()
	private(set) var activeTabID: String?
	private(set) var activeProfileID: String?
	// Profiles open in the sidebar. Not persisted: collapsed on every launch.
	var expandedProfileIDs: Set<String> = []
	var page: Page = .browser

	// Session state of live pages, by tab id.
	private(set) var loadedTabIDs: Set<String> = []
	private(set) var loadingProgress: [String: Double] = [:]
	private(set) var navigation: [String: NavigationState] = [:]

	struct NavigationState: Equatable {
		var canGoBack = false
		var canGoForward = false
	}

	private let profilesFile: PersistedFile<ProfilesState>
	private let settingsFile: PersistedFile<SettingsState>
	private var saveTask: Task<Void, Never>?

	init(dataDirectory: URL) {
		profilesFile = PersistedFile(directory: dataDirectory, name: "profiles")
		settingsFile = PersistedFile(directory: dataDirectory, name: "settings")
		profiles = Self.load(profilesFile)?.profiles ?? []
		settings = Self.load(settingsFile)?.settings ?? .defaults
	}

	private static func load<State>(_ file: PersistedFile<State>) -> State? {
		do {
			return try file.load()
		} catch {
			NSLog("Pane: could not read %@: %@", file.url.path, String(describing: error))
			return nil
		}
	}

	// MARK: Lookups

	func profile(_ id: String) -> BrowserProfile? {
		profiles.first { $0.id == id }
	}

	func profileID(ofTab tabID: String) -> String? {
		profiles.first { $0.tabs.contains { $0.id == tabID } }?.id
	}

	func tab(_ id: String) -> Tab? {
		for profile in profiles {
			if let tab = profile.tabs.first(where: { $0.id == id }) {
				return tab
			}
		}

		return nil
	}

	var activeTab: Tab? {
		activeTabID.flatMap(tab)
	}

	// MARK: Profiles

	@discardableResult
	func createProfile(name: String, color: ProfileColor, group: String?) -> String {
		let now = Self.timestamp()
		let profile = BrowserProfile(
			id: UUID().uuidString.lowercased(),
			name: name,
			color: color,
			group: group,
			createdAt: now,
			updatedAt: now
		)
		profiles.append(profile)
		scheduleSave()

		return profile.id
	}

	func updateProfile(_ id: String, _ change: (inout BrowserProfile) -> Void) {
		guard let index = profiles.firstIndex(where: { $0.id == id }) else {
			return
		}

		change(&profiles[index])
		profiles[index].updatedAt = Self.timestamp()
		scheduleSave()
	}

	func removeProfile(_ id: String) {
		guard let profile = profile(id) else {
			return
		}

		for tab in profile.tabs {
			forgetTab(tab.id)
		}

		profiles.removeAll { $0.id == id }
		expandedProfileIDs.remove(id)

		if activeProfileID == id {
			activeProfileID = nil
			activeTabID = nil
		}

		scheduleSave()
	}

	func moveProfile(from source: Int, to destination: Int) {
		guard profiles.indices.contains(source) else {
			return
		}

		let profile = profiles.remove(at: source)
		profiles.insert(profile, at: min(destination, profiles.count))
		scheduleSave()
	}

	// MARK: Tabs

	func insertTab(_ tab: Tab, inProfile profileID: String, after anchorID: String? = nil) {
		guard let index = profiles.firstIndex(where: { $0.id == profileID }) else {
			return
		}

		let position = anchorID
			.flatMap { anchor in profiles[index].tabs.firstIndex { $0.id == anchor } }
			.map { $0 + 1 } ?? profiles[index].tabs.count
		profiles[index].tabs.insert(tab, at: position)
		scheduleSave()
	}

	func updateTab(_ id: String, _ change: (inout Tab) -> Void) {
		for profileIndex in profiles.indices {
			if let tabIndex = profiles[profileIndex].tabs.firstIndex(where: { $0.id == id }) {
				let before = profiles[profileIndex].tabs[tabIndex]
				change(&profiles[profileIndex].tabs[tabIndex])

				if profiles[profileIndex].tabs[tabIndex] != before {
					scheduleSave()
				}

				return
			}
		}
	}

	// Removes the tab and remembers it for Reopen closed tab.
	@discardableResult
	func removeTab(_ id: String) -> Tab? {
		for profileIndex in profiles.indices {
			if let tabIndex = profiles[profileIndex].tabs.firstIndex(where: { $0.id == id }) {
				let tab = profiles[profileIndex].tabs.remove(at: tabIndex)
				history.pushClosed(ClosedTab(url: tab.url, profileID: profiles[profileIndex].id, title: tab.title, favicon: tab.favicon))
				forgetTab(id)
				scheduleSave()

				return tab
			}
		}

		return nil
	}

	func popClosedTab() -> ClosedTab? {
		history.popClosed()
	}

	func moveTab(inProfile profileID: String, from source: Int, to destination: Int) {
		guard
			let index = profiles.firstIndex(where: { $0.id == profileID }),
			profiles[index].tabs.indices.contains(source)
		else {
			return
		}

		let tab = profiles[index].tabs.remove(at: source)
		profiles[index].tabs.insert(tab, at: min(destination, profiles[index].tabs.count))
		scheduleSave()
	}

	// MARK: Selection and live state

	func select(tabID: String?) {
		activeTabID = tabID

		if let tabID {
			activeProfileID = profileID(ofTab: tabID)
			history.use(tabID)
		}
	}

	func setActiveProfile(_ id: String?) {
		activeProfileID = id
	}

	func setLoaded(_ tabID: String, _ loaded: Bool) {
		if loaded {
			loadedTabIDs.insert(tabID)
		} else {
			loadedTabIDs.remove(tabID)
			loadingProgress[tabID] = nil
			navigation[tabID] = nil
		}
	}

	func setLoading(_ tabID: String, progress: Double?) {
		loadingProgress[tabID] = progress
	}

	func setNavigation(_ tabID: String, _ state: NavigationState) {
		navigation[tabID] = state
	}

	private func forgetTab(_ id: String) {
		history.forget(id)
		setLoaded(id, false)

		if activeTabID == id {
			activeTabID = nil
		}
	}

	// MARK: Persistence

	private func scheduleSave() {
		saveTask?.cancel()
		saveTask = Task { [weak self] in
			try? await Task.sleep(for: .milliseconds(300))

			guard !Task.isCancelled else {
				return
			}

			self?.saveNow()
		}
	}

	func saveNow() {
		saveTask?.cancel()
		saveTask = nil

		do {
			try profilesFile.save(ProfilesState(profiles: profiles))
			try settingsFile.save(SettingsState(settings: settings))
		} catch {
			NSLog("Pane: could not save state: %@", String(describing: error))
		}
	}

	private static func timestamp() -> String {
		let formatter = ISO8601DateFormatter()
		formatter.formatOptions = [.withInternetDateTime, .withFractionalSeconds]

		return formatter.string(from: Date())
	}
}
