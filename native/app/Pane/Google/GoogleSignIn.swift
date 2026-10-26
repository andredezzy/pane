import Foundation
import PaneCore
import PaneKit

// Google's rejection of the engine ("this browser may not be secure"),
// handled as the Electron app did (apps/veil profile/google/sign-in.ts): a
// real Chrome opens to sign in, the tab shows how to bring the session back,
// and Transfer imports Chrome's Google cookies into the profile, then
// continues where Google was going. It only starts when Google rejects the
// engine, so it costs nothing when Google accepts it.
@MainActor
final class GoogleSignIn {
	struct State: Equatable {
		var transferring = false
		var error: String?
	}

	private struct Flow {
		let profileID: String
		let next: URL
		let chrome: ChromeAuth
		let timeout: Task<Void, Never>
	}

	// An unfinished sign-in gives up after five minutes, and Chrome quits.
	private static let timeout = Duration.seconds(5 * 60)

	private let model: BrowserModel
	private let context: (String) -> BrowserContext?
	private let load: (String, URL) -> Void
	private let sessionDidChange: (String) -> Void
	private var flows: [String: Flow] = [:]
	// Scenarios point it at their own pages.
	private let accountsOrigin = ProcessInfo.processInfo.environment["PANE_GOOGLE_ACCOUNTS_ORIGIN"]
		.flatMap(URL.init(string:)) ?? GoogleAccounts.accountsOrigin

	// `load` opens a URL in a tab; `sessionDidChange` follows a profile whose
	// Google session changed.
	init(
		model: BrowserModel,
		context: @escaping (String) -> BrowserContext?,
		load: @escaping (String, URL) -> Void,
		sessionDidChange: @escaping (String) -> Void
	) {
		self.model = model
		self.context = context
		self.load = load
		self.sessionDidChange = sessionDidChange
	}

	// A tab's page changed to `url`. True while the tab is on Google's
	// rejection, which then shows the transfer page and stays out of the
	// tab's history; a page elsewhere ends the tab's sign-in.
	func handle(_ url: URL, tabID: String, profileID: String) -> Bool {
		guard let next = GoogleAccounts.rejection(url, accountsOrigin: accountsOrigin) else {
			cancel(tabID)
			return false
		}

		if flows[tabID] != nil {
			return true
		}

		guard let chromePath = ChromeAuth.chromePath(settings: model.settings) else {
			NSLog("Pane: Google rejected the engine, and there is no Chrome to sign in with")
			return false
		}

		do {
			let chrome = try ChromeAuth(
				chromePath: chromePath,
				loginURL: GoogleAccounts.loginURL(continue: next, accountsOrigin: accountsOrigin)
			)
			let timeout = Task { [weak self] in
				try? await Task.sleep(for: Self.timeout)

				if !Task.isCancelled {
					self?.cancel(tabID)
				}
			}

			flows[tabID] = Flow(profileID: profileID, next: next, chrome: chrome, timeout: timeout)
			model.googleSignIns[tabID] = State()

			return true
		} catch {
			NSLog("Pane: could not open Chrome to sign in to Google: %@", String(describing: error))
			return false
		}
	}

	// The transfer page's button: brings Chrome's Google session into the
	// profile. Chrome stays open after a failure, so the user can finish
	// signing in and try again.
	func transfer(_ tabID: String) {
		guard let flow = flows[tabID], model.googleSignIns[tabID]?.transferring == false,
		      let context = context(flow.profileID)
		else {
			return
		}

		model.googleSignIns[tabID] = State(transferring: true)

		Task { [weak self] in
			do {
				let cookies = try await flow.chrome.googleCookies()
				let imported = await GoogleSession.importCookies(cookies, into: context)

				guard let self, flows[tabID] != nil else {
					return
				}

				guard imported > 0 else {
					model.googleSignIns[tabID] = State(error: "No Google cookies found. Make sure you completed sign-in in the Chrome window.")
					return
				}

				cancel(tabID)
				load(tabID, flow.next)
				sessionDidChange(flow.profileID)
			} catch {
				if let self, flows[tabID] != nil {
					model.googleSignIns[tabID] = State(error: error.localizedDescription)
				}
			}
		}
	}

	// Ends the tab's sign-in, if it has one: Chrome quits and its temporary
	// profile goes.
	func cancel(_ tabID: String) {
		guard let flow = flows.removeValue(forKey: tabID) else {
			return
		}

		flow.timeout.cancel()
		flow.chrome.stop()
		model.googleSignIns[tabID] = nil
	}

	// The temporary profile of the tab's Chrome, for scenarios.
	func chromeProfile(of tabID: String) -> URL? {
		flows[tabID]?.chrome.directory
	}

	func cancelAll() {
		for tabID in Array(flows.keys) {
			cancel(tabID)
		}
	}
}
