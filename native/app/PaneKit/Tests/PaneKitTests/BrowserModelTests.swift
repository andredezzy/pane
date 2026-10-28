import Foundation
import PaneKit
import Testing

// The model's clock, driven by hand: the time it stamps, and the saves it
// waits to run, fired by the test.
@MainActor
private final class ManualClock {
	var date = Date(timeIntervalSince1970: 1_800_000_000)
	private(set) var waiting: [(delay: Duration, work: @MainActor () -> Void, id: Int)] = []
	private var nextID = 0

	var clock: BrowserModel.Clock {
		// Weakly: a test may keep the model alone.
		BrowserModel.Clock(now: { [weak self] in self?.date ?? .distantPast }) { [weak self] delay, work in
			guard let self else {
				return {}
			}

			let id = nextID
			nextID += 1
			waiting.append((delay, work, id))

			return { [weak self] in self?.waiting.removeAll { $0.id == id } }
		}
	}

	// Runs what waits, as its delay passing would.
	func fire() {
		let due = waiting
		waiting = []
		due.forEach { $0.work() }
	}
}

@MainActor
private final class Harness {
	let clock = ManualClock()
	var saves: [BrowserModel.Save] = []
	private(set) var model: BrowserModel!

	init(_ profiles: [BrowserProfile] = [], settings: AppSettings = .defaults) {
		model = BrowserModel(profiles: profiles, settings: settings, clock: clock.clock) { [weak self] save in
			self?.saves.append(save)
		}
	}
}

private func profile(_ id: String, tabs: [String] = []) -> BrowserProfile {
	BrowserProfile(id: id, name: id, tabs: tabs.map { Tab(id: $0, url: "https://\($0).example/", title: $0.uppercased()) }, createdAt: "", updatedAt: "")
}

@MainActor
private func tabIDs(_ model: BrowserModel, _ profileID: String) -> [String] {
	model.profile(profileID)?.tabs.map(\.id) ?? []
}

@MainActor
@Suite("browser model")
struct BrowserModelTests {
	// MARK: Tabs

	@Test("inserts a tab after its anchor, else at the end")
	func insertsAfterAnchor() {
		let model = Harness([profile("p", tabs: ["a", "b", "c"])]).model!

		model.insertTab(Tab(id: "x", url: ""), inProfile: "p", after: "a")
		#expect(tabIDs(model, "p") == ["a", "x", "b", "c"])

		model.insertTab(Tab(id: "y", url: ""), inProfile: "p")
		#expect(tabIDs(model, "p") == ["a", "x", "b", "c", "y"])

		// An anchor that is not the profile's puts it last.
		model.insertTab(Tab(id: "z", url: ""), inProfile: "p", after: "elsewhere")
		#expect(tabIDs(model, "p").last == "z")

		// No such profile: nothing.
		model.insertTab(Tab(id: "w", url: ""), inProfile: "none")
		#expect(model.tab("w") == nil)
	}

	@Test("removing a tab remembers it, forgets its use, and clears its selection")
	func removesToHistory() {
		let harness = Harness([profile("p", tabs: ["a", "b"])])
		let model = harness.model!
		model.updateTab("a") { $0.favicon = "data:image/png;base64,AA" }
		model.select(tabID: "b")
		model.select(tabID: "a")
		model.setLoaded("a", true)

		let removed = model.removeTab("a")

		#expect(removed?.id == "a")
		#expect(tabIDs(model, "p") == ["b"])
		#expect(model.activeTabID == nil)
		// The profile stays active: its next tab takes the place.
		#expect(model.activeProfileID == "p")
		#expect(model.history.mru == ["b"])
		#expect(!model.loadedTabIDs.contains("a"))
		#expect(model.history.closed.first == ClosedTab(url: "https://a.example/", profileID: "p", title: "A", favicon: "data:image/png;base64,AA"))

		#expect(model.popClosedTab()?.url == "https://a.example/")
		#expect(model.popClosedTab() == nil)
		#expect(model.removeTab("none") == nil)
	}

	@Test("removing a profile forgets its tabs, collapses it, and clears its selection")
	func removesProfile() {
		let model = Harness([profile("p", tabs: ["a"]), profile("q", tabs: ["b"])]).model!
		model.setExpanded("p", true)
		model.setProxyStatus("p", .connected)
		model.select(tabID: "b")
		model.select(tabID: "a")

		model.removeProfile("p")

		#expect(model.profiles.map(\.id) == ["q"])
		#expect(model.activeTabID == nil)
		#expect(model.activeProfileID == nil)
		#expect(model.expandedProfileIDs.isEmpty)
		#expect(model.proxyStatus["p"] == nil)
		#expect(model.history.mru == ["b"])
		// Deleting a profile is no closed tab.
		#expect(model.history.closed.isEmpty)
	}

	@Test("selecting a tab makes its profile active and it the most recent")
	func selects() {
		let model = Harness([profile("p", tabs: ["a"]), profile("q", tabs: ["b"])]).model!

		model.select(tabID: "a")
		model.select(tabID: "b")

		#expect(model.activeTabID == "b")
		#expect(model.activeProfileID == "q")
		#expect(model.activeTab?.id == "b")
		#expect(model.history.mru == ["b", "a"])

		// No tab keeps the profile.
		model.select(tabID: nil)
		#expect(model.activeTabID == nil)
		#expect(model.activeProfileID == "q")
	}

	@Test("every change of the selection reaches each observer")
	func selectionObservers() {
		let model = Harness([profile("p", tabs: ["a", "b"])]).model!
		var first = 0
		var second = 0
		model.observeSelection { first += 1 }
		model.observeSelection { second += 1 }

		// The tab, then its profile.
		model.select(tabID: "a")
		#expect(first == 2)

		model.setActiveProfile(nil)
		model.removeTab("b")
		#expect(first == 3)

		// The selected tab going clears it.
		model.select(tabID: "a")
		let before = first
		model.removeTab("a")
		#expect(first == before + 1)
		#expect(second == first)
	}

	@Test("moves clamp past either end, and ignore a source that is not there")
	func movesClamp() {
		let model = Harness([profile("p", tabs: ["a", "b", "c"]), profile("q"), profile("r")]).model!

		model.moveTab(inProfile: "p", from: 0, to: 99)
		#expect(tabIDs(model, "p") == ["b", "c", "a"])

		model.moveTab(inProfile: "p", from: 2, to: -5)
		#expect(tabIDs(model, "p") == ["a", "b", "c"])

		model.moveTab(inProfile: "p", from: 7, to: 0)
		model.moveTab(inProfile: "none", from: 0, to: 1)
		#expect(tabIDs(model, "p") == ["a", "b", "c"])

		model.moveProfile(from: 0, to: 10)
		#expect(model.profiles.map(\.id) == ["q", "r", "p"])

		model.moveProfile(from: 1, to: 0)
		#expect(model.profiles.map(\.id) == ["r", "q", "p"])

		model.moveProfile(from: 3, to: 0)
		#expect(model.profiles.map(\.id) == ["r", "q", "p"])
	}

	// MARK: Profiles

	@Test("an edit reports what the engine must apply: its proxy, its fingerprint")
	func detectsChanges() {
		let model = Harness([profile("p")]).model!
		let proxy = ProxyConfig(proxyType: .http, host: "proxy.example", port: 8080)

		#expect(model.updateProfile("p") { $0.name = "Renamed" } == [])
		#expect(model.updateProfile("p") { $0.proxy = proxy } == .proxy)
		#expect(model.updateProfile("p") { $0.proxy = proxy } == [])
		#expect(model.updateProfile("p") { $0.fingerprint = .preset(.windows) } == .fingerprint)
		#expect(model.updateProfile("p") {
			$0.proxy = nil
			$0.fingerprint = nil
		} == [.proxy, .fingerprint])
		#expect(model.updateProfile("none") { $0.proxy = proxy } == [])
		#expect(model.profile("p")?.name == "Renamed")
	}

	@Test("profiles take the clock's time, created and updated")
	func timestamps() {
		let harness = Harness()
		let model = harness.model!

		let id = model.createProfile(name: "Work", color: .rose, group: "Clients")
		let created = model.profile(id)

		#expect(created?.createdAt == "2027-01-15T08:00:00.000Z")
		#expect(created?.updatedAt == created?.createdAt)
		#expect(created?.name == "Work")
		#expect(created?.color == .rose)
		#expect(created?.group == "Clients")

		harness.clock.date = harness.clock.date.addingTimeInterval(1.5)
		model.updateProfile(id) { $0.keepLoaded = true }

		#expect(model.profile(id)?.createdAt == "2027-01-15T08:00:00.000Z")
		#expect(model.profile(id)?.updatedAt == "2027-01-15T08:00:01.500Z")
	}

	// MARK: Live state

	@Test("an unloaded tab keeps nothing of its page")
	func unloadClearsPageState() {
		let model = Harness([profile("p", tabs: ["a"])]).model!
		model.setLoaded("a", true)
		model.setLoading("a", progress: 0.4)
		model.setNavigation("a", .init(canGoBack: true))
		model.setCrashed("a", true)
		model.setPictureInPicture("a", true)
		model.setDevToolsOpen("a", true)

		model.setLoaded("a", false)

		#expect(model.loadedTabIDs.isEmpty)
		#expect(model.loadingProgress.isEmpty)
		#expect(model.loadingSince.isEmpty)
		#expect(model.navigation.isEmpty)
		#expect(model.crashedTabIDs.isEmpty)
		#expect(model.pictureInPictureTabIDs.isEmpty)
		#expect(model.devToolsTabIDs.isEmpty)
		#expect(model.tab("a") != nil)
	}

	@Test("a load keeps the time it began until it ends, through its progress")
	func loadingSince() {
		let harness = Harness([profile("p", tabs: ["a"])])
		let model = harness.model!
		let began = harness.clock.date

		model.setLoading("a", progress: 0.1)
		harness.clock.date = began.addingTimeInterval(2)
		model.setLoading("a", progress: 0.6)

		#expect(model.loadingSince["a"] == began)

		model.setLoading("a", progress: nil)
		#expect(model.loadingSince["a"] == nil)

		model.setLoading("a", progress: 0.1)
		#expect(model.loadingSince["a"] == began.addingTimeInterval(2))
	}

	// MARK: Saving

	@Test("a burst of changes saves once, after the delay, the profiles alone")
	func debouncesSave() {
		let harness = Harness([profile("p", tabs: ["a"])])
		let model = harness.model!

		model.updateTab("a") { $0.url = "https://moved.example/" }
		model.updateTab("a") { $0.title = "Moved" }
		model.insertTab(Tab(id: "b", url: ""), inProfile: "p")

		// One wait, the last change's: each change put the save off again.
		#expect(harness.saves.isEmpty)
		#expect(harness.clock.waiting.count == 1)
		#expect(harness.clock.waiting.first?.delay == BrowserModel.saveDelay)

		harness.clock.fire()

		#expect(harness.saves.count == 1)
		#expect(harness.saves.first?.profiles?.first?.tabs.map(\.title) == ["Moved", ""])
		#expect(harness.saves.first?.settings == nil)
	}

	@Test("a settings change saves the settings alone, and only when they change")
	func savesSettings() {
		let harness = Harness([profile("p")])
		let model = harness.model!

		model.changeSettings { $0.theme = AppSettings.defaults.theme }
		#expect(harness.clock.waiting.isEmpty)

		model.changeSettings { $0.theme = .dark }
		harness.clock.fire()

		#expect(harness.saves.count == 1)
		#expect(harness.saves.first?.settings?.theme == .dark)
		#expect(harness.saves.first?.profiles == nil)
	}

	@Test("a page reporting what the tab already holds saves nothing")
	func unchangedTabSavesNothing() {
		let harness = Harness([profile("p", tabs: ["a"])])

		harness.model.updateTab("a") { $0.title = "A" }

		#expect(harness.clock.waiting.isEmpty)
	}

	@Test("saveNow writes what changed at once, and nothing when nothing did")
	func savesNow() {
		let harness = Harness([profile("p")])
		let model = harness.model!

		model.saveNow()
		#expect(harness.saves.isEmpty)

		model.setExpanded("p", true)
		model.saveNow()
		// Expansion is the session's: nothing to write.
		#expect(harness.saves.isEmpty)

		model.createProfile(name: "New", color: .blue, group: nil)
		model.changeSettings { $0.sidebarWidth = 300 }
		model.saveNow()

		#expect(harness.saves.count == 1)
		#expect(harness.saves.first?.profiles?.count == 2)
		#expect(harness.saves.first?.settings?.sidebarWidth == 300)
		// The wait it replaced is gone.
		#expect(harness.clock.waiting.isEmpty)

		harness.clock.fire()
		#expect(harness.saves.count == 1)
	}

	@Test("a failed write asks again, and the next save writes that file")
	func retriesFailedWrite() {
		let harness = Harness([profile("p")])

		harness.model.scheduleSave(profiles: true)
		harness.clock.fire()

		#expect(harness.saves.count == 1)
		#expect(harness.saves.first?.profiles?.map(\.id) == ["p"])
	}
}
