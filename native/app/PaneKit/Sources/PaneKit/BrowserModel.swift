import Foundation
import Observation

// Everything the app shows: profiles and their tabs (persisted), settings
// (persisted), and the session's selection, expansion, and page state.
// Views read it; BrowserController changes it, as the user acts and the
// engine reports. It knows no engine and no file: what to write goes to
// `persist`, and time comes from its clock, so its rules run in tests.
@MainActor
@Observable
public final class BrowserModel {
	public enum Page: Sendable {
		case browser
		case settings
		case extensions
	}

	public enum ProxyStatus: String, Sendable {
		case testing
		case connected
		case failed
	}

	public struct NavigationState: Equatable, Sendable {
		public var canGoBack = false
		public var canGoForward = false

		public init(canGoBack: Bool = false, canGoForward: Bool = false) {
			self.canGoBack = canGoBack
			self.canGoForward = canGoForward
		}
	}

	// What a profile's edit changed that the engine has to apply.
	public struct ProfileChanges: OptionSet, Sendable {
		public let rawValue: Int

		public init(rawValue: Int) {
			self.rawValue = rawValue
		}

		public static let proxy = ProfileChanges(rawValue: 1 << 0)
		public static let fingerprint = ProfileChanges(rawValue: 1 << 1)
	}

	// What a save writes: a copy of the state of each file that changed,
	// nil for one that did not.
	public struct Save: Sendable {
		public var profiles: [BrowserProfile]?
		public var settings: AppSettings?
	}

	// The model's time: the stamps it puts on profiles, and the wait before
	// a save. Tests drive both by hand.
	public struct Clock {
		public var now: () -> Date
		// Runs `work` after `delay`, unless the closure it returns is
		// called first.
		public var after: (_ delay: Duration, _ work: @escaping @MainActor () -> Void) -> () -> Void

		public init(now: @escaping () -> Date, after: @escaping (Duration, @escaping @MainActor () -> Void) -> () -> Void) {
			self.now = now
			self.after = after
		}

		public static var system: Clock {
			Clock(now: Date.init) { delay, work in
				let task = Task { @MainActor in
					try? await Task.sleep(for: delay)

					if !Task.isCancelled {
						work()
					}
				}

				return { task.cancel() }
			}
		}
	}

	// A change saves this long after the last one, so a burst (a page's
	// URL, then its title, then its favicon) writes once.
	public static let saveDelay = Duration.milliseconds(300)

	public private(set) var profiles: [BrowserProfile]
	public private(set) var settings: AppSettings

	public private(set) var history = TabHistory()
	public private(set) var activeTabID: String? {
		didSet { selectionDidChange() }
	}
	public private(set) var activeProfileID: String? {
		didSet { selectionDidChange() }
	}
	// Profiles open in the sidebar. Not persisted: collapsed on every launch.
	public private(set) var expandedProfileIDs: Set<String> = []
	public private(set) var page: Page = .browser

	// Session state of live pages, by tab id.
	public private(set) var loadedTabIDs: Set<String> = []
	public private(set) var loadingProgress: [String: Double] = [:]
	// When each loading tab's load began: a tab coming forward mid-load shows
	// its progress where it would be had it been shown all along.
	public private(set) var loadingSince: [String: Date] = [:]
	public private(set) var navigation: [String: NavigationState] = [:]
	// Tabs whose page crashed and waits for Reload (the sad tab).
	public private(set) var crashedTabIDs: Set<String> = []
	public private(set) var pictureInPictureTabIDs: Set<String> = []
	public private(set) var devToolsTabIDs: Set<String> = []
	// Each proxied profile's last connection test (the Electron app's
	// proxy-status-store), for the sidebar's dot.
	public private(set) var proxyStatus: [String: ProxyStatus] = [:]

	@ObservationIgnored private let clock: Clock
	@ObservationIgnored private let persist: (Save) -> Void
	// Called when the selected tab or profile changes: the sleep clocks.
	@ObservationIgnored private var selectionObservers: [() -> Void] = []
	@ObservationIgnored private var cancelSave: (() -> Void)?
	// What changed since the last save: only that file is written.
	@ObservationIgnored private var profilesChanged = false
	@ObservationIgnored private var settingsChanged = false

	public init(profiles: [BrowserProfile], settings: AppSettings, clock: Clock = .system, persist: @escaping (Save) -> Void) {
		self.profiles = profiles
		self.settings = settings
		self.clock = clock
		self.persist = persist
	}

	// Every change of the selection calls `observer`, each one added.
	public func observeSelection(_ observer: @escaping () -> Void) {
		selectionObservers.append(observer)
	}

	private func selectionDidChange() {
		selectionObservers.forEach { $0() }
	}

	// MARK: Lookups

	public func profile(_ id: String) -> BrowserProfile? {
		profiles.first { $0.id == id }
	}

	public func profileID(ofTab tabID: String) -> String? {
		profiles.first { $0.tabs.contains { $0.id == tabID } }?.id
	}

	public func tab(_ id: String) -> Tab? {
		for profile in profiles {
			if let tab = profile.tabs.first(where: { $0.id == id }) {
				return tab
			}
		}

		return nil
	}

	public var activeTab: Tab? {
		activeTabID.flatMap(tab)
	}

	// MARK: Settings

	// Saves only a change: the same value again writes nothing.
	public func changeSettings(_ change: (inout AppSettings) -> Void) {
		var changed = settings
		change(&changed)

		if changed != settings {
			settings = changed
			scheduleSave(settings: true)
		}
	}

	// MARK: Profiles

	@discardableResult
	public func createProfile(name: String, color: ProfileColor, group: String?) -> String {
		createProfile {
			$0.name = name
			$0.color = color
			$0.group = group
		}
	}

	// A new profile, at the end, as `configure` makes it.
	@discardableResult
	public func createProfile(_ configure: (inout BrowserProfile) -> Void) -> String {
		let now = timestamp()
		var profile = BrowserProfile(id: UUID().uuidString.lowercased(), name: "", createdAt: now, updatedAt: now)
		configure(&profile)
		profiles.append(profile)
		scheduleSave(profiles: true)

		return profile.id
	}

	// What the change did to the profile's proxy and fingerprint, for the
	// engine to apply.
	@discardableResult
	public func updateProfile(_ id: String, _ change: (inout BrowserProfile) -> Void) -> ProfileChanges {
		guard let index = profiles.firstIndex(where: { $0.id == id }) else {
			return []
		}

		let proxy = profiles[index].proxy
		let fingerprint = profiles[index].fingerprint
		change(&profiles[index])
		profiles[index].updatedAt = timestamp()
		scheduleSave(profiles: true)

		var changes = ProfileChanges()

		if profiles[index].proxy != proxy {
			changes.insert(.proxy)
		}

		if profiles[index].fingerprint != fingerprint {
			changes.insert(.fingerprint)
		}

		return changes
	}

	public func removeProfile(_ id: String) {
		guard let profile = profile(id) else {
			return
		}

		for tab in profile.tabs {
			forgetTab(tab.id)
		}

		profiles.removeAll { $0.id == id }
		expandedProfileIDs.remove(id)
		proxyStatus[id] = nil

		if activeProfileID == id {
			activeProfileID = nil
			activeTabID = nil
		}

		scheduleSave(profiles: true)
	}

	// `destination` past the end puts it last.
	public func moveProfile(from source: Int, to destination: Int) {
		guard profiles.indices.contains(source) else {
			return
		}

		let profile = profiles.remove(at: source)
		profiles.insert(profile, at: min(max(destination, 0), profiles.count))
		scheduleSave(profiles: true)
	}

	public func setExpanded(_ id: String, _ expanded: Bool) {
		if expanded {
			expandedProfileIDs.insert(id)
		} else {
			expandedProfileIDs.remove(id)
		}
	}

	public func setProxyStatus(_ id: String, _ status: ProxyStatus?) {
		proxyStatus[id] = status
	}

	// MARK: Tabs

	// After the anchor tab, or last without one (or when it is not the
	// profile's).
	public func insertTab(_ tab: Tab, inProfile profileID: String, after anchorID: String? = nil) {
		guard let index = profiles.firstIndex(where: { $0.id == profileID }) else {
			return
		}

		let position = anchorID
			.flatMap { anchor in profiles[index].tabs.firstIndex { $0.id == anchor } }
			.map { $0 + 1 } ?? profiles[index].tabs.count
		profiles[index].tabs.insert(tab, at: position)
		scheduleSave(profiles: true)
	}

	// Saves only a change: a page reporting the same title writes nothing.
	public func updateTab(_ id: String, _ change: (inout Tab) -> Void) {
		for profileIndex in profiles.indices {
			if let tabIndex = profiles[profileIndex].tabs.firstIndex(where: { $0.id == id }) {
				let before = profiles[profileIndex].tabs[tabIndex]
				change(&profiles[profileIndex].tabs[tabIndex])

				if profiles[profileIndex].tabs[tabIndex] != before {
					scheduleSave(profiles: true)
				}

				return
			}
		}
	}

	// Removes the tab and remembers it for Reopen closed tab.
	@discardableResult
	public func removeTab(_ id: String) -> Tab? {
		for profileIndex in profiles.indices {
			if let tabIndex = profiles[profileIndex].tabs.firstIndex(where: { $0.id == id }) {
				let tab = profiles[profileIndex].tabs.remove(at: tabIndex)
				history.pushClosed(ClosedTab(url: tab.url, profileID: profiles[profileIndex].id, title: tab.title, favicon: tab.favicon))
				forgetTab(id)
				scheduleSave(profiles: true)

				return tab
			}
		}

		return nil
	}

	public func popClosedTab() -> ClosedTab? {
		history.popClosed()
	}

	// `destination` past the end puts it last.
	public func moveTab(inProfile profileID: String, from source: Int, to destination: Int) {
		guard
			let index = profiles.firstIndex(where: { $0.id == profileID }),
			profiles[index].tabs.indices.contains(source)
		else {
			return
		}

		let tab = profiles[index].tabs.remove(at: source)
		profiles[index].tabs.insert(tab, at: min(max(destination, 0), profiles[index].tabs.count))
		scheduleSave(profiles: true)
	}

	// MARK: Selection and live state

	// A tab makes its profile the active one, and the most recently used.
	public func select(tabID: String?) {
		activeTabID = tabID

		if let tabID {
			activeProfileID = profileID(ofTab: tabID)
			history.use(tabID)
		}
	}

	public func setActiveProfile(_ id: String?) {
		activeProfileID = id
	}

	public func setPage(_ page: Page) {
		self.page = page
	}

	// An unloaded tab has no page, so nothing of a page's state stays.
	public func setLoaded(_ tabID: String, _ loaded: Bool) {
		if loaded {
			loadedTabIDs.insert(tabID)
		} else {
			loadedTabIDs.remove(tabID)
			loadingProgress[tabID] = nil
			loadingSince[tabID] = nil
			navigation[tabID] = nil
			crashedTabIDs.remove(tabID)
			pictureInPictureTabIDs.remove(tabID)
			devToolsTabIDs.remove(tabID)
		}
	}

	public func setLoading(_ tabID: String, progress: Double?) {
		if progress == nil {
			loadingSince[tabID] = nil
		} else if loadingProgress[tabID] == nil {
			loadingSince[tabID] = clock.now()
		}

		loadingProgress[tabID] = progress
	}

	public func setNavigation(_ tabID: String, _ state: NavigationState) {
		navigation[tabID] = state
	}

	public func setCrashed(_ tabID: String, _ crashed: Bool) {
		if crashed {
			crashedTabIDs.insert(tabID)
		} else {
			crashedTabIDs.remove(tabID)
		}
	}

	public func setPictureInPicture(_ tabID: String, _ active: Bool) {
		if active {
			pictureInPictureTabIDs.insert(tabID)
		} else {
			pictureInPictureTabIDs.remove(tabID)
		}
	}

	public func setDevToolsOpen(_ tabID: String, _ open: Bool) {
		if open {
			devToolsTabIDs.insert(tabID)
		} else {
			devToolsTabIDs.remove(tabID)
		}
	}

	private func forgetTab(_ id: String) {
		history.forget(id)
		setLoaded(id, false)

		if activeTabID == id {
			activeTabID = nil
		}
	}

	// MARK: Persistence

	// `saveDelay` after the last change, the files that changed are handed
	// to `persist`. A title, URL, or favicon change saves too, so the model
	// hands over a copy of the state, for the encoding and the write to run
	// off the main thread. A write that failed asks again here.
	public func scheduleSave(profiles: Bool = false, settings: Bool = false) {
		profilesChanged = profilesChanged || profiles
		settingsChanged = settingsChanged || settings
		cancelSave?()
		cancelSave = clock.after(Self.saveDelay) { [weak self] in
			self?.saveNow()
		}
	}

	// What changed, handed to `persist` at once: at quit, and for scenarios
	// that read the files.
	public func saveNow() {
		cancelSave?()
		cancelSave = nil

		guard profilesChanged || settingsChanged else {
			return
		}

		let save = Save(profiles: profilesChanged ? profiles : nil, settings: settingsChanged ? settings : nil)
		profilesChanged = false
		settingsChanged = false
		persist(save)
	}

	private func timestamp() -> String {
		let formatter = ISO8601DateFormatter()
		formatter.formatOptions = [.withInternetDateTime, .withFractionalSeconds]

		return formatter.string(from: clock.now())
	}
}
