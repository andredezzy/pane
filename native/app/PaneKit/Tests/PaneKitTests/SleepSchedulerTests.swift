import Foundation
@testable import PaneKit
import Testing

// Ported from apps/veil sleep-scheduler.test.ts: every case, against fake
// tabs and caches, with a clock the tests move one scheduler tick at a time
// (the Electron tests' fake timers).

@MainActor
private final class FakeTabs: SleepableTabs {
	var loaded: [String]
	// Replaces the loaded tabs the scheduler sees (the Electron tests'
	// mockReturnValue).
	var loadedOverride: [String]?
	var protected: (String) -> Bool = { _ in false }
	private(set) var slept: [String] = []
	private(set) var unloads = 0

	init(_ tabIDs: [String] = ["tab-1"]) {
		loaded = tabIDs
	}

	func loadedTabIDs() -> [String] {
		loadedOverride ?? loaded
	}

	func isProtected(_ tabID: String) -> Bool {
		protected(tabID)
	}

	func sleep(_ tabID: String) {
		slept.append(tabID)
		loaded.removeAll { $0 == tabID }
	}

	func unloadAll() {
		unloads += 1
		loaded = []
		loadedOverride = nil
	}
}

private struct DiskBusy: Error {}

@MainActor
private final class FakeCache: TrimmableCache {
	enum Trim {
		case succeeds
		case neverFinishes
		case fails
	}

	let sizeMB: Int
	var trimBehavior = Trim.succeeds
	private(set) var sizeReads = 0
	private(set) var trims = 0

	init(sizeMB: Int = 0) {
		self.sizeMB = sizeMB
	}

	func size() async throws -> Int {
		sizeReads += 1
		return sizeMB * 1024 * 1024
	}

	func trim() async throws {
		trims += 1

		switch trimBehavior {
		case .succeeds:
			return
		case .neverFinishes:
			try await Task.sleep(for: .seconds(3600))
		case .fails:
			throw DiskBusy()
		}
	}
}

// A scheduler with its fake clock and settings, ticking every minute as the
// app's timer does.
@MainActor
private final class Harness {
	var clock = Date(timeIntervalSince1970: 1_800_000_000)
	var settings = AppSettings(tabSleepAfterMinutes: 15, profileUnloadAfterMinutes: 30, cacheBudgetMB: 300)
	var profiles: [String: SleepScheduler.Profile] = [:]
	var warnings: [String] = []
	private(set) var scheduler: SleepScheduler!
	private var elapsedSinceTick: TimeInterval = 0

	// The profile, its fakes, and whether it is Keep loaded.
	func add(_ id: String, tabs: FakeTabs = FakeTabs(), cache: FakeCache = FakeCache(), keepLoaded: Bool = false) {
		profiles[id] = SleepScheduler.Profile(tabs: tabs, cache: cache, keepLoaded: keepLoaded)
	}

	func start(activeProfile: String? = nil, activeTab: String? = nil) {
		scheduler = SleepScheduler(
			profileIDs: Array(profiles.keys),
			activeProfileID: activeProfile,
			activeTabID: activeTab,
			now: { [unowned self] in clock },
			warn: { [unowned self] in warnings.append($0) }
		)
	}

	func activate(tab: String?, profile: String?) {
		scheduler.activeDidChange(profileID: profile, tabID: tab)
	}

	func advance(minutes: Int) {
		for _ in 0 ..< minutes {
			clock += 60
			scheduler.tick(profiles: profiles, settings: settings)
		}
	}

	// Advances, then lets the trims started on the way finish.
	func advanceSettling(minutes: Int) async {
		advance(minutes: minutes)

		for _ in 0 ..< 10 {
			await Task.yield()
		}
	}
}

@Suite("SleepScheduler · profile unload")
@MainActor
struct ProfileUnloadTests {
	@Test("unloads a hidden profile once the timer elapses")
	func unloadsHidden() {
		let harness = Harness()
		let tabs = FakeTabs()
		harness.add("hidden", tabs: tabs)
		harness.start()

		harness.advance(minutes: 31)

		#expect(tabs.unloads == 1)
	}

	@Test("never unloads the active profile")
	func neverActive() {
		let harness = Harness()
		let tabs = FakeTabs()
		harness.add("active", tabs: tabs)
		harness.start(activeProfile: "active", activeTab: "tab-1")

		harness.advance(minutes: 31)

		#expect(tabs.unloads == 0)
	}

	@Test("skips Keep loaded profiles")
	func skipsKeepLoaded() {
		let harness = Harness()
		let tabs = FakeTabs()
		harness.add("kept", tabs: tabs, keepLoaded: true)
		harness.start()

		harness.advance(minutes: 31)

		#expect(tabs.unloads == 0)
		#expect(tabs.slept.isEmpty)
	}

	@Test("skips profiles holding a protected tab")
	func skipsProtected() {
		let harness = Harness()
		let tabs = FakeTabs()
		tabs.protected = { _ in true }
		harness.add("hidden", tabs: tabs)
		harness.start()

		harness.advance(minutes: 31)

		#expect(tabs.unloads == 0)
	}

	@Test("skips already-unloaded profiles")
	func skipsUnloaded() {
		let harness = Harness()
		let tabs = FakeTabs([])
		harness.add("hidden", tabs: tabs)
		harness.start()

		harness.advance(minutes: 31)

		#expect(tabs.unloads == 0)
	}

	@Test("does nothing when the timer setting is off")
	func offSetting() {
		let harness = Harness()
		harness.settings.profileUnloadAfterMinutes = nil
		let tabs = FakeTabs()
		harness.add("hidden", tabs: tabs)
		harness.start()

		harness.advance(minutes: 31)

		#expect(tabs.unloads == 0)
	}

	@Test("restarts the hidden clock when a profile is re-activated")
	func restartsHiddenClock() {
		// Tab sleep stays off, so it cannot drain the profile first: this
		// isolates the profile clock.
		let harness = Harness()
		harness.settings.tabSleepAfterMinutes = nil
		let tabs = FakeTabs()
		harness.add("profile", tabs: tabs)
		harness.start()

		// 20 minutes hidden, then a visit resets the clock; 20 more minutes
		// after switching away must not reach the 30-minute deadline.
		harness.advance(minutes: 20)
		harness.activate(tab: "tab-1", profile: "profile")
		harness.activate(tab: nil, profile: nil)
		harness.advance(minutes: 20)

		#expect(tabs.unloads == 0)

		harness.advance(minutes: 11)

		#expect(tabs.unloads == 1)
	}
}

@Suite("SleepScheduler · tab sleep")
@MainActor
struct TabSleepTests {
	@Test("sleeps a background tab once the timer elapses")
	func sleepsBackground() {
		let harness = Harness()
		let tabs = FakeTabs(["tab-1", "tab-2"])
		harness.add("active", tabs: tabs)
		harness.start()

		// tab-1 goes to the background when tab-2 takes over.
		harness.activate(tab: "tab-1", profile: "active")
		harness.activate(tab: "tab-2", profile: "active")
		harness.advance(minutes: 16)

		#expect(tabs.slept == ["tab-1"])
	}

	@Test("never sleeps the active tab")
	func neverActive() {
		let harness = Harness()
		let tabs = FakeTabs(["tab-1"])
		harness.add("active", tabs: tabs)
		harness.start()

		harness.activate(tab: "tab-1", profile: "active")
		harness.advance(minutes: 16)

		#expect(tabs.slept.isEmpty)
	}

	@Test("skips protected background tabs")
	func skipsProtected() {
		let harness = Harness()
		let tabs = FakeTabs(["tab-1", "tab-2"])
		tabs.protected = { $0 == "tab-1" }
		harness.add("active", tabs: tabs)
		harness.start()

		harness.activate(tab: "tab-1", profile: "active")
		harness.activate(tab: "tab-2", profile: "active")
		harness.advance(minutes: 16)

		#expect(tabs.slept.isEmpty)
	}

	@Test("does nothing when the tab timer setting is off")
	func offSetting() {
		let harness = Harness()
		harness.settings.tabSleepAfterMinutes = nil
		let tabs = FakeTabs(["tab-1", "tab-2"])
		harness.add("active", tabs: tabs)
		harness.start()

		harness.activate(tab: "tab-1", profile: "active")
		harness.activate(tab: "tab-2", profile: "active")
		harness.advance(minutes: 31)

		#expect(tabs.slept.isEmpty)
	}

	@Test("restarts the background clock when a tab is re-activated")
	func restartsBackgroundClock() {
		let harness = Harness()
		let tabs = FakeTabs(["tab-1", "tab-2"])
		harness.add("active", tabs: tabs)
		harness.start()

		harness.activate(tab: "tab-1", profile: "active")
		harness.activate(tab: "tab-2", profile: "active")
		harness.advance(minutes: 10)
		harness.activate(tab: "tab-1", profile: "active")
		harness.activate(tab: "tab-2", profile: "active")
		harness.advance(minutes: 10)

		#expect(tabs.slept.isEmpty)

		harness.advance(minutes: 6)

		#expect(tabs.slept == ["tab-1"])
	}
}

// The trim hangs off the unload, so every case advances past the 30-minute
// unload deadline, then lets the trim run.
@Suite("SleepScheduler · cache trim")
@MainActor
struct CacheTrimTests {
	@Test("trims a profile that unloads over budget")
	func overBudget() async {
		let harness = Harness()
		let cache = FakeCache(sizeMB: 400)
		harness.add("hidden", cache: cache)
		harness.start()

		await harness.advanceSettling(minutes: 31)

		#expect(cache.trims == 1)
	}

	@Test("leaves a profile that unloads under budget")
	func underBudget() async {
		let harness = Harness()
		let cache = FakeCache(sizeMB: 50)
		harness.add("hidden", cache: cache)
		harness.start()

		await harness.advanceSettling(minutes: 31)

		#expect(cache.sizeReads == 1)
		#expect(cache.trims == 0)
	}

	@Test("never measures the cache when the budget setting is off")
	func budgetOff() async {
		let harness = Harness()
		harness.settings.cacheBudgetMB = nil
		let cache = FakeCache(sizeMB: 4000)
		harness.add("hidden", cache: cache)
		harness.start()

		await harness.advanceSettling(minutes: 31)

		#expect(cache.sizeReads == 0)
		#expect(cache.trims == 0)
	}

	@Test("never trims a Keep loaded profile, however large")
	func keepLoaded() async {
		let harness = Harness()
		let cache = FakeCache(sizeMB: 4000)
		harness.add("kept", cache: cache, keepLoaded: true)
		harness.start()

		await harness.advanceSettling(minutes: 31)

		#expect(cache.trims == 0)
	}

	@Test("never trims on the tab-sleep path")
	func tabSleepPath() async {
		let harness = Harness()
		harness.settings.profileUnloadAfterMinutes = nil
		let tabs = FakeTabs(["tab-1", "tab-2"])
		let cache = FakeCache(sizeMB: 4000)
		harness.add("active", tabs: tabs, cache: cache)
		harness.start()

		harness.activate(tab: "tab-1", profile: "active")
		harness.activate(tab: "tab-2", profile: "active")
		await harness.advanceSettling(minutes: 31)

		#expect(!tabs.slept.isEmpty)
		#expect(cache.trims == 0)
	}

	// Not in the Electron tests: its scheduler never trimmed this profile.
	@Test("trims a hidden profile that tab sleep drained")
	func drainedByTabSleep() async {
		let harness = Harness()
		let tabs = FakeTabs(["tab-1", "tab-2"])
		let cache = FakeCache(sizeMB: 400)
		harness.add("hidden", tabs: tabs, cache: cache)
		harness.start()

		// Shown, then hidden: both its tabs are in the background.
		harness.activate(tab: "tab-1", profile: "hidden")
		harness.activate(tab: "tab-2", profile: "hidden")
		harness.activate(tab: nil, profile: nil)
		await harness.advanceSettling(minutes: 16)

		#expect(tabs.slept.sorted() == ["tab-1", "tab-2"])
		#expect(tabs.unloads == 0)
		#expect(cache.trims == 1)
	}

	@Test("leaves a drained profile's cache when unloading is off")
	func drainedWithUnloadOff() async {
		let harness = Harness()
		harness.settings.profileUnloadAfterMinutes = nil
		let tabs = FakeTabs(["tab-1"])
		let cache = FakeCache(sizeMB: 400)
		harness.add("hidden", tabs: tabs, cache: cache)
		harness.start()

		harness.activate(tab: "tab-1", profile: "hidden")
		harness.activate(tab: nil, profile: nil)
		await harness.advanceSettling(minutes: 16)

		#expect(tabs.slept == ["tab-1"])
		#expect(cache.sizeReads == 0)
	}

	@Test("does not stack a second trim while one is in flight")
	func noStacking() async {
		let harness = Harness()
		let tabs = FakeTabs()
		let cache = FakeCache(sizeMB: 400)
		// The first trim stays in flight across later ticks.
		cache.trimBehavior = .neverFinishes
		harness.add("hidden", tabs: tabs, cache: cache)
		harness.start()

		await harness.advanceSettling(minutes: 31)

		// Hidden again, so the unload is reachable on a later tick.
		harness.activate(tab: "tab-1", profile: "hidden")
		harness.activate(tab: nil, profile: nil)
		tabs.loadedOverride = ["tab-1"]
		await harness.advanceSettling(minutes: 31)

		#expect(tabs.unloads == 2)
		#expect(cache.trims == 1)

		harness.scheduler.trims["hidden"]?.cancel()
	}

	@Test("keeps ticking after a trim fails")
	func afterFailure() async {
		let harness = Harness()
		let tabs = FakeTabs()
		let cache = FakeCache(sizeMB: 400)
		cache.trimBehavior = .fails
		harness.add("hidden", tabs: tabs, cache: cache)
		harness.start()

		await harness.advanceSettling(minutes: 31)
		await harness.scheduler.trims["hidden"]?.value

		#expect(!harness.warnings.isEmpty)

		// The in-flight guard let go, so a later unload trims again.
		harness.activate(tab: "tab-1", profile: "hidden")
		harness.activate(tab: nil, profile: nil)
		tabs.loadedOverride = ["tab-1"]
		await harness.advanceSettling(minutes: 31)
		await harness.scheduler.trims["hidden"]?.value

		#expect(cache.trims == 2)
	}
}
