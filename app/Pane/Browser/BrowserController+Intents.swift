import PaneKit
import SwiftUI

// What the user asks of the browser's profiles, pages, and settings, from
// the sidebar, the profile sheet, Settings, and the menu bar alike. Each
// change of the model's state goes through here, with what it makes the
// engine do, so the UI and Automation (which calls these in the UI's place)
// cannot drift apart.
extension BrowserController {
	// MARK: Profiles

	// The profile sheet's Create: the profile as the form has it, expanded.
	@discardableResult
	func createProfile(_ draft: ProfileDraft) -> String {
		let id = model.createProfile { draft.apply(to: &$0, generate: generateFingerprint(draft)) }
		let profile = model.profile(id)
		var changes = BrowserModel.ProfileChanges()

		if profile?.proxy != nil {
			changes.insert(.proxy)
		}

		if profile?.fingerprint != nil {
			changes.insert(.fingerprint)
		}

		apply(changes, to: id)
		expandProfile(id)

		return id
	}

	// The profile sheet's Save.
	func saveProfile(_ id: String, _ draft: ProfileDraft) {
		updateProfile(id) { draft.apply(to: &$0, generate: generateFingerprint(draft)) }
	}

	// Any edit; a new proxy or fingerprint reaches the engine.
	func updateProfile(_ id: String, _ change: (inout BrowserProfile) -> Void) {
		apply(model.updateProfile(id, change), to: id)
	}

	func setKeepLoaded(_ id: String, _ keepLoaded: Bool) {
		updateProfile(id) { $0.keepLoaded = keepLoaded }
	}

	func moveProfile(from source: Int, to destination: Int) {
		model.moveProfile(from: source, to: destination)
	}

	func moveTab(inProfile profileID: String, from source: Int, to destination: Int) {
		model.moveTab(inProfile: profileID, from: source, to: destination)
	}

	// The sheet, sliding in from the right as its scrim fades (DESIGN.md:
	// enter and exit animate); nil takes it away.
	func presentProfileSheet(_ target: WindowState.ProfileSheetTarget?) {
		withAnimation(Motion.sheet) {
			state.profileSheet = target
		}
	}

	private func generateFingerprint(_ draft: ProfileDraft) -> () -> Fingerprint {
		let hostWebGL = hostWebGL

		return { Fingerprint.generated(for: draft.platform, hostWebGL: hostWebGL) }
	}

	private func apply(_ changes: BrowserModel.ProfileChanges, to id: String) {
		if changes.contains(.proxy) {
			proxyDidChange(id)
		}

		if changes.contains(.fingerprint) {
			fingerprintDidChange(id)
		}
	}

	// MARK: Expanding and collapsing

	// A profile opens in the sidebar; its tabs load as they are selected.
	func expandProfile(_ id: String) {
		model.setExpanded(id, true)
	}

	// Collapsing unloads: the profile's pages go, its tabs stay (CONTEXT.md:
	// Unload).
	func collapseProfile(_ id: String) {
		model.setExpanded(id, false)
		unloadProfile(id)
	}

	// A click on a profile's header.
	func toggleProfile(_ id: String) {
		if model.expandedProfileIDs.contains(id) {
			collapseProfile(id)
		} else {
			expandProfile(id)
		}
	}

	// The sidebar's Collapse all.
	func collapseAll() {
		for id in model.expandedProfileIDs {
			collapseProfile(id)
		}
	}

	// MARK: Pages

	// The browser, Settings, or the Extensions page. Under the latter two,
	// which cover it, the selected tab's page leaves the panel and stops
	// drawing (placeActivePage).
	func showPage(_ page: BrowserModel.Page) {
		model.setPage(page)
		placeActivePage()

		// Back on the browser, its page takes the keys again.
		if page == .browser {
			activePage?.focus()
		}
	}

	// The sidebar's Settings: there, or back to the browser.
	func toggleSettings() {
		showPage(model.page == .settings ? .browser : .settings)
	}

	// MARK: Settings

	// A change of the theme applies to the app, its menus, and its pages at
	// once (ADR 0001); one of the app icon, to the Dock (ADR 0089).
	func changeSettings(_ change: (inout AppSettings) -> Void) {
		let theme = model.settings.theme
		let appIcon = model.settings.appIcon
		model.changeSettings(change)

		if model.settings.theme != theme {
			applyTheme()
		}

		if model.settings.appIcon != appIcon {
			dockIcon.show(model.settings.appIcon)
		}
	}
}
