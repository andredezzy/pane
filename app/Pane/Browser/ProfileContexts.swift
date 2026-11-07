import Foundation
import PaneCore
import PaneKit

// Each profile's BrowserContext in the engine: opening one on first use, once the
// volume has its folders (ADR 0113), closing it, deleting it, what a lock closes
// and waits for (ADR 0116), and its history's choice, with the clear it owes (ADR
// 0119). The browser holds the pages that run in them, and the model the profiles.
@MainActor
final class ProfileContexts {
	private unowned let browser: BrowserController
	private let application: BrowserApplication
	private var contexts: [String: BrowserContext] = [:]
	private var contextWaiters: [String: [(BrowserContext?) -> Void]] = [:]
	// Closed profiles opened only to clear their history (historyDidChange).
	private var openedToClearHistory: Set<String> = []
	// The profiles whose clear the engine is running, and what ends each one's
	// wait for it to reach the disk.
	private var clearingHistory: Set<String> = []
	private var historyClearSettling: [String: DispatchWorkItem] = [:]
	// Open profiles whose caches are on disk: all that save history, and one
	// turned off since it opened, until it opens again.
	private var contextsCachingOnDisk: Set<String> = []
	// The downloads each open profile is running, which its closing would end.
	private var runningDownloads: [String: Set<String>] = [:]
	// Contexts the engine is opening or deleting, for a lock to wait for.
	private var engineWork: [ObjectIdentifier: EngineWork] = [:]

	private var model: BrowserModel {
		browser.model
	}

	init(browser: BrowserController, application: BrowserApplication) {
		self.browser = browser
		self.application = application
	}

	subscript(profileID: String) -> BrowserContext? {
		contexts[profileID]
	}

	// Runs `body` with the profile's context, opening it first if needed;
	// with nil when it would not open, so no caller is left half done.
	func with(_ profileID: String, _ body: @escaping (BrowserContext?) -> Void) {
		if let context = contexts[profileID] {
			body(context)
			return
		}

		if contextWaiters[profileID] != nil {
			contextWaiters[profileID]?.append(body)
			return
		}

		contextWaiters[profileID] = [body]
		let work = beginWork()

		// The profile's engine data is in the volume: its context opens only
		// once the volume is attached and the profile's links lead into it,
		// so nothing of it lands in the clear (ADR 0113).
		browser.vault.whenAttached { [weak self] attached in
			guard let self else {
				return
			}

			guard attached else {
				opened(profileID, nil, work)
				return
			}

			browser.vault.prepare(profile: profileID, savesHistory: model.profile(profileID)?.savesHistory ?? true) { [weak self] prepared in
				guard let self else {
					return
				}

				guard prepared, work.lock == nil else {
					opened(profileID, nil, work)
					return
				}

				openContext(profileID, work)
			}
		}
	}

	private func openContext(_ profileID: String, _ work: EngineWork) {
		applyFingerprint(profileID)

		let savesHistory = model.profile(profileID)?.savesHistory ?? true
		application.setKeepsCachesInMemory(!savesHistory, forContextIdentifier: profileID)

		// Gone only behind a lock, whose work closes as it opens.
		guard let engineKey = browser.vault.engineKey(profile: profileID) else {
			opened(profileID, nil, work)
			return
		}

		application.openContext(identifier: profileID, engineKey: engineKey) { [weak self] context, error in
			guard let self else {
				return
			}

			guard let context else {
				Log.engine.error("Could not open profile \(profileID): \(String(describing: error), privacy: .private)")
				opened(profileID, nil, work)
				return
			}

			// A lock came first: the context closes for it, as it is.
			guard work.lock == nil else {
				opened(profileID, context, work)
				return
			}

			context.delegate = browser

			if savesHistory {
				contextsCachingOnDisk.insert(profileID)
			}

			applyHistory(profileID, to: context)

			// The proxy goes on before any page of the profile loads.
			Task { [weak self] in
				guard let self else {
					return
				}

				await browser.network.apply(to: context)
				opened(profileID, context, work)
			}
		}
	}

	// The open ended: what waited has the context, or nil when it would not
	// open. A lock that came first closes it instead.
	private func opened(_ profileID: String, _ context: BrowserContext?, _ work: EngineWork) {
		endWork(work)

		if let closing = work.lock {
			if let context {
				closing.close(context, in: application)
			}

			closing.finish(true)
			return
		}

		let waiters = contextWaiters.removeValue(forKey: profileID) ?? []

		if let context {
			contexts[profileID] = context
			browser.extensions.refresh(profileID)
			browser.vault.profileDidOpen(profileID)
		}

		waiters.forEach { $0(context) }
	}

	private func beginWork() -> EngineWork {
		let work = EngineWork()
		engineWork[ObjectIdentifier(work)] = work
		return work
	}

	private func endWork(_ work: EngineWork) {
		engineWork[ObjectIdentifier(work)] = nil
	}

	// The profile's fingerprint, or Chrome's own values without one. It goes
	// to the engine before the profile's context opens, since its network
	// context takes the User-Agent as it is created. A change reaches
	// navigations and new renderers at once; open pages keep their renderer's.
	func applyFingerprint(_ profileID: String) {
		do {
			let json = try model.profile(profileID)?.fingerprint.map { fingerprint in
				try EngineFingerprint(
					fingerprint: fingerprint,
					profileID: profileID,
					engineUserAgent: application.userAgent,
					engineVersion: application.chromiumVersion
				).json()
			}

			try application.setFingerprint(json, forContextIdentifier: profileID)
		} catch {
			Log.engine.error("Could not apply the fingerprint of profile \(profileID): \(String(describing: error), privacy: .private)")
		}
	}

	// Save history changed (ADR 0119). Off clears what the profile kept, now,
	// opening it if it is closed, as the sheet says; on waits for it to open.
	func historyDidChange(_ profileID: String) {
		if let context = contexts[profileID] {
			applyHistory(profileID, to: context)
			return
		}

		guard model.profile(profileID)?.clearsHistory == true else {
			return
		}

		// Opening for something else, it stays open for that.
		if contextWaiters[profileID] == nil {
			openedToClearHistory.insert(profileID)
		}

		with(profileID) { [weak self] context in
			guard let self else {
				return
			}

			guard let context else {
				openedToClearHistory.remove(profileID)
				return
			}

			applyHistory(profileID, to: context)
			closeIfOpenedToClearHistory(profileID)
		}
	}

	// The choice reaches the engine, and a clear still owed runs. Both are made
	// up as the profile opens, whatever a quit, a crash, or a lock cut short.
	private func applyHistory(_ profileID: String, to context: BrowserContext) {
		guard let profile = model.profile(profileID) else {
			return
		}

		if context.savesHistory != profile.savesHistory {
			context.savesHistory = profile.savesHistory
		}

		guard profile.clearsHistory, clearingHistory.insert(profileID).inserted else {
			return
		}

		historyClearSettling[profileID]?.cancel()
		model.forgetClosedTabs(inProfile: profileID)

		// On the engine's UI thread, as its header doesn't say.
		context.clearHistory { [weak self] in
			MainActor.assumeIsolated {
				self?.historyCleared(profileID)
			}
		}
	}

	// Done in the engine, on disk within its 10-second commit: until then a
	// crash loses the deletion, and the clear stays owed.
	private func historyCleared(_ profileID: String) {
		clearingHistory.remove(profileID)
		closeIfOpenedToClearHistory(profileID)
		reopenCachingInMemory(profileID)

		let settled = DispatchWorkItem { [weak self] in
			self?.historyClearSettling[profileID] = nil
			self?.model.updateProfile(profileID) { $0.clearsHistory = false }
		}

		historyClearSettling[profileID] = settled
		DispatchQueue.main.asyncAfter(deadline: .now() + 15, execute: settled)
	}

	// A closed profile opened only to clear closes again, its memory and its
	// proxy's relay with it, once it has both opened and cleared, unless
	// something has come to use it meanwhile. What asks for it as it closes
	// waits, and opens it again.
	private func closeIfOpenedToClearHistory(_ profileID: String) {
		guard
			openedToClearHistory.contains(profileID),
			!clearingHistory.contains(profileID),
			let context = contexts[profileID]
		else {
			return
		}

		openedToClearHistory.remove(profileID)

		guard
			model.page != .extensions,
			model.profile(profileID)?.tabs.allSatisfy({ browser.page(for: $0.id) == nil }) == true
		else {
			return
		}

		close(profileID, context)
	}

	// A profile turned off while open was still writing its caches to disk, and
	// only a new context stops that: its pages start again, as for a new
	// fingerprint, the selected one at once. A download it is running would end,
	// so then the caches stay until the profile next opens, which removes them
	// (DataVolume.prepare).
	private func reopenCachingInMemory(_ profileID: String) {
		guard
			contextsCachingOnDisk.contains(profileID),
			runningDownloads[profileID]?.isEmpty != false,
			let context = contexts[profileID],
			let profile = model.profile(profileID),
			!profile.savesHistory
		else {
			return
		}

		let selected = model.activeTabID

		for tab in profile.tabs {
			browser.release(tabID: tab.id)
		}

		close(profileID, context)

		if let selected, profile.tabs.contains(where: { $0.id == selected }) {
			browser.select(tabID: selected)
		} else if model.page == .extensions {
			browser.extensions.openContextIfNeeded(profileID: profileID)
		}
	}

	// What asks for the profile as it closes waits, and opens it again. Closed,
	// a clear it ran is on disk, and no longer owed.
	private func close(_ profileID: String, _ context: BrowserContext) {
		contexts[profileID] = nil
		contextsCachingOnDisk.remove(profileID)
		runningDownloads[profileID] = nil
		browser.network.stop(profileID)
		contextWaiters[profileID] = []
		let work = beginWork()

		application.closeContext(context) { [weak self] closed in
			guard let self else {
				return
			}

			endWork(work)
			work.lock?.finish(closed)

			if closed, work.lock == nil {
				historyClearSettling.removeValue(forKey: profileID)?.cancel()
				model.updateProfile(profileID) { $0.clearsHistory = false }
			}

			let waiters = contextWaiters.removeValue(forKey: profileID) ?? []
			waiters.forEach { self.with(profileID, $0) }
		}
	}

	// Every profile closes in the engine, one opening or being deleted once that
	// is done; what waited for a context gets none. What says whether every one
	// closed, its files with it, which the volume's detach waits for.
	func closeForLock() -> Task<Bool, Never> {
		let closing = ContextClosing()

		for context in contexts.values {
			closing.close(context, in: application)
		}

		contexts.removeAll()
		// A clear cut short here is still owed, at the next open.
		clearingHistory.removeAll()
		openedToClearHistory.removeAll()
		contextsCachingOnDisk.removeAll()
		runningDownloads.removeAll()
		engineWork.values.forEach { $0.wait(for: closing) }
		engineWork.removeAll()
		let waiting = contextWaiters.values.joined()
		contextWaiters.removeAll()
		closing.seal()

		let closed = Task { await closing.closed }
		browser.vault.lock(once: closed)
		waiting.forEach { $0(nil) }

		return closed
	}

	// The profile's data goes in the engine, a profile not opened this session
	// opened first. `done` says whether its folder in the volume goes now: the
	// engine removes the profile's link, and when it would not open, or once a
	// lock closed the volume, the next open removes the folder (ProfileDeletions).
	func delete(_ profileID: String, done: @escaping (_ removesFolder: Bool) -> Void) {
		with(profileID) { [weak self] context in
			guard let self else {
				return
			}

			guard let context else {
				done(false)
				return
			}

			contexts[profileID] = nil
			let work = beginWork()

			application.deleteContext(context) { [weak self] deleted in
				self?.endWork(work)
				// A lock waited for the profile to go.
				work.lock?.finish(deleted)
				done(deleted && work.lock == nil)
			}
		}
	}

	// A running download keeps its profile's context open (reopenCachingInMemory).
	func downloadDidUpdate(_ download: Download, in context: BrowserContext) {
		if download.state == .inProgress {
			runningDownloads[context.identifier, default: []].insert(download.identifier)
		} else {
			runningDownloads[context.identifier]?.remove(download.identifier)
		}
	}
}
