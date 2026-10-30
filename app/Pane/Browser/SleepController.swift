import Foundation
import PaneCore
import PaneKit

// Runs the sleep scheduler (CONTEXT.md: Sleep, Unload, Trim, Keep loaded) over
// the open profiles: a timer ticks it every minute, and the selection moves
// its clocks. Sleep releases a tab's page and keeps the tab, as the Electron
// app did; the page comes back by navigating again when the tab is selected.
@MainActor
final class SleepController {
	// Debug only: PANE_TIME_SCALE=60 makes a minute pass every second, for the
	// sleep scenario.
	private static let timeScale = ProcessInfo.processInfo.environment["PANE_TIME_SCALE"]
		.flatMap(Double.init).map { max($0, 1) } ?? 1

	private unowned let controller: BrowserController
	private let scheduler: SleepScheduler
	private var timer: Timer?

	init(controller: BrowserController) {
		self.controller = controller
		let model = controller.model
		let start = Date()
		let scale = Self.timeScale

		scheduler = SleepScheduler(
			profileIDs: model.profiles.map(\.id),
			activeProfileID: model.activeProfileID,
			activeTabID: model.activeTabID,
			now: { start.addingTimeInterval(Date().timeIntervalSince(start) * scale) }
		)

		model.observeSelection { [weak self] in
			guard let self else {
				return
			}

			scheduler.activeDidChange(profileID: self.controller.model.activeProfileID, tabID: self.controller.model.activeTabID)
		}

		let timer = Timer(timeInterval: SleepScheduler.tickInterval / scale, repeats: true) { [weak self] _ in
			MainActor.assumeIsolated { self?.tick() }
		}
		// A tenth of slack, as Apple's energy guide asks of a repeating timer.
		timer.tolerance = timer.timeInterval / 10
		RunLoop.main.add(timer, forMode: .common)
		self.timer = timer
	}

	private func tick() {
		var profiles: [String: SleepScheduler.Profile] = [:]

		for profile in controller.model.profiles {
			guard let context = controller.context(for: profile.id) else {
				continue
			}

			profiles[profile.id] = SleepScheduler.Profile(
				tabs: ProfileTabs(controller: controller, profileID: profile.id),
				cache: ProfileCache(context: context),
				keepLoaded: profile.keepLoaded
			)
		}

		scheduler.tick(profiles: profiles, settings: controller.model.settings)
	}
}

// A profile's live pages.
@MainActor
private final class ProfileTabs: SleepableTabs {
	private unowned let controller: BrowserController
	private let profileID: String

	init(controller: BrowserController, profileID: String) {
		self.controller = controller
		self.profileID = profileID
	}

	func loadedTabIDs() -> [String] {
		(controller.model.profile(profileID)?.tabs ?? []).map(\.id).filter { controller.page(for: $0) != nil }
	}

	// Playing sound, inspected in DevTools, or shown in picture-in-picture: in
	// use, though not selected.
	func isProtected(_ tabID: String) -> Bool {
		guard let page = controller.page(for: tabID) else {
			return false
		}

		return page.isAudible || page.isDevToolsOpen || controller.model.pictureInPictureTabIDs.contains(tabID)
	}

	func sleep(_ tabID: String) {
		controller.release(tabID: tabID)
	}

	func unloadAll() {
		controller.unloadProfile(profileID)
	}
}

// A profile's cache, measured and trimmed by the engine.
@MainActor
private final class ProfileCache: TrimmableCache {
	private let context: BrowserContext

	init(context: BrowserContext) {
		self.context = context
	}

	func size() async throws -> Int {
		Int(await context.cacheSize())
	}

	func trim() async throws {
		await context.trimCache()
	}
}
