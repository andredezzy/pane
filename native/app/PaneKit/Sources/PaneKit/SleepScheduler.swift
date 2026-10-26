import Foundation

// What the scheduler needs from a profile: tabs whose memory it can release,
// and a cache whose disk it can release. Narrow on purpose, so the scheduler
// runs against fakes in tests.
@MainActor
public protocol SleepableTabs: AnyObject {
	func loadedTabIDs() -> [String]
	// Audible, DevTools open, or otherwise in use: never sleeps.
	func isProtected(_ tabID: String) -> Bool
	func sleep(_ tabID: String)
	func unloadAll()
}

@MainActor
public protocol TrimmableCache: AnyObject {
	// Bytes on disk.
	func size() async throws -> Int
	func trim() async throws
}

// Puts background tabs to sleep, unloads hidden profiles once their settings
// timers elapse, and trims the cache of a profile that unloads over budget
// (CONTEXT.md: Sleep, Unload, Trim; ported from apps/veil sleep-scheduler.ts).
// Nothing sleeps while active, protected, or in a Keep loaded profile; a clock
// restarts when its tab or profile is activated again, so recent interaction
// is protected by construction. The app calls tick() every tickInterval.
@MainActor
public final class SleepScheduler {
	public struct Profile {
		public let tabs: any SleepableTabs
		public let cache: any TrimmableCache
		public let keepLoaded: Bool

		public init(tabs: any SleepableTabs, cache: any TrimmableCache, keepLoaded: Bool) {
			self.tabs = tabs
			self.cache = cache
			self.keepLoaded = keepLoaded
		}
	}

	public static let tickInterval: TimeInterval = 60
	private static let bytesPerMB = 1024 * 1024

	private let now: () -> Date
	private let warn: (String) -> Void
	private var profileHiddenSince: [String: Date] = [:]
	private var tabBackgroundSince: [String: Date] = [:]
	private var trimming: Set<String> = []
	private var activeProfileID: String?
	private var activeTabID: String?
	// Each profile's last trim, for tests to wait on.
	private(set) var trims: [String: Task<Void, Never>] = [:]

	// Every profile but the active one starts hidden now.
	public init(
		profileIDs: [String],
		activeProfileID: String?,
		activeTabID: String?,
		now: @escaping () -> Date = Date.init,
		warn: @escaping (String) -> Void = { NSLog("%@", $0) }
	) {
		self.now = now
		self.warn = warn
		self.activeProfileID = activeProfileID
		self.activeTabID = activeTabID

		let start = now()

		for id in profileIDs where id != activeProfileID {
			profileHiddenSince[id] = start
		}
	}

	// The selected profile or tab changed: the one left starts its clock, the
	// one shown stops it.
	public func activeDidChange(profileID: String?, tabID: String?) {
		if profileID != activeProfileID {
			if let previous = activeProfileID {
				profileHiddenSince[previous] = now()
			}

			if let profileID {
				profileHiddenSince[profileID] = nil
			}
		}

		if tabID != activeTabID {
			if let previous = activeTabID {
				tabBackgroundSince[previous] = now()
			}

			if let tabID {
				tabBackgroundSince[tabID] = nil
			}
		}

		activeProfileID = profileID
		activeTabID = tabID
	}

	public func tick(profiles: [String: Profile], settings: AppSettings) {
		let current = now()

		for (profileID, profile) in profiles {
			let loadedTabIDs = profile.tabs.loadedTabIDs()

			if loadedTabIDs.isEmpty || profile.keepLoaded {
				continue
			}

			if let minutes = settings.profileUnloadAfterMinutes,
			   profileID != activeProfileID,
			   elapsed(profileHiddenSince[profileID], minutes, current),
			   !loadedTabIDs.contains(where: profile.tabs.isProtected)
			{
				profile.tabs.unloadAll()
				trim(profileID, profile.cache, budgetMB: settings.cacheBudgetMB)

				// The hidden clock restarts on the next hide: a tab appearing
				// in a still-hidden profile (opened by an extension, say) must
				// not unload at once against this stale stamp.
				profileHiddenSince[profileID] = nil

				continue
			}

			guard let minutes = settings.tabSleepAfterMinutes else {
				continue
			}

			for tabID in loadedTabIDs
				where tabID != activeTabID
				&& elapsed(tabBackgroundSince[tabID], minutes, current)
				&& !profile.tabs.isProtected(tabID)
			{
				profile.tabs.sleep(tabID)
				tabBackgroundSince[tabID] = nil
			}

			// A hidden profile whose last tabs just slept has unloaded too, and
			// is trimmed as one. A hidden profile's tabs are all in the
			// background, so with tab sleep sooner than unload (the defaults)
			// they always drain first; the Electron scheduler then never
			// trimmed at all.
			if settings.profileUnloadAfterMinutes != nil, profileID != activeProfileID,
			   profile.tabs.loadedTabIDs().isEmpty
			{
				trim(profileID, profile.cache, budgetMB: settings.cacheBudgetMB)
				profileHiddenSince[profileID] = nil
			}
		}

		pruneTabStamps(profiles)
	}

	private func elapsed(_ since: Date?, _ minutes: Int, _ now: Date) -> Bool {
		guard let since else {
			return false
		}

		return now.timeIntervalSince(since) >= TimeInterval(minutes * 60)
	}

	// Started, never awaited: a slow disk must not stall the tick. The
	// in-flight set stops a later tick stacking a second trim on the same
	// profile. A failed trim is logged and dropped; the next unload retries.
	private func trim(_ profileID: String, _ cache: any TrimmableCache, budgetMB: Int?) {
		guard let budgetMB, !trimming.contains(profileID) else {
			return
		}

		trimming.insert(profileID)

		trims[profileID] = Task { [weak self] in
			do {
				if try await cache.size() > budgetMB * Self.bytesPerMB {
					try await cache.trim()
				}
			} catch {
				self?.warn("Pane: trimming the cache of profile \(profileID) failed: \(error)")
			}

			self?.trimming.remove(profileID)
		}
	}

	// Closed tabs never become loaded again, so their stamps would pile up
	// for the life of the app without this sweep.
	private func pruneTabStamps(_ profiles: [String: Profile]) {
		let loaded = Set(profiles.values.flatMap { $0.tabs.loadedTabIDs() })

		for tabID in tabBackgroundSince.keys where !loaded.contains(tabID) {
			tabBackgroundSince[tabID] = nil
		}
	}
}
