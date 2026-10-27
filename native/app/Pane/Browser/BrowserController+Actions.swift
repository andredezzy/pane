import AppKit
import PaneKit

// Menu and keyboard actions, and the app-wide theme.
extension BrowserController {
	// MARK: Theme (ADR 0001: one knob for the app, its menus, and pages)

	func setTheme(_ theme: Theme) {
		model.settings.theme = theme
		applyTheme()
	}

	func applyTheme() {
		switch model.settings.theme {
		case .system: NSApp.appearance = nil
		case .light: NSApp.appearance = NSAppearance(named: .aqua)
		case .dark: NSApp.appearance = NSAppearance(named: .darkAqua)
		}
	}

	// MARK: Ctrl+Tab switcher

	func openSwitcher(backward: Bool) {
		let openProfiles = model.profiles.filter { model.expandedProfileIDs.contains($0.id) }
		let groups = TabSwitcher.groups(mru: model.history.mru, profiles: openProfiles)
		let flattened = groups.flatMap(\.tabs)

		guard !flattened.isEmpty else {
			return
		}

		let mru = model.history.mru
		var index = TabSwitcher.initialSelectedIndex(flattened, previousTabID: mru.count > 1 ? mru[1] : nil)

		if backward {
			index = TabSwitcher.cycle(0, by: -1, count: flattened.count)
		}

		state.switcher = .init(visible: true, groups: groups, selectedIndex: index)
	}

	func cycleSwitcher(backward: Bool) {
		let count = state.switcher.flattened.count
		state.switcher.selectedIndex = TabSwitcher.cycle(state.switcher.selectedIndex, by: backward ? -1 : 1, count: count)
	}

	func commitSwitcher() {
		let flattened = state.switcher.flattened
		let index = state.switcher.selectedIndex
		state.switcher = .init()

		if flattened.indices.contains(index) {
			select(tabID: flattened[index].id)
		}
	}

	func cancelSwitcher() {
		state.switcher = .init()
	}

	// MARK: Menu actions

	// The app menu's Check for Updates…: the pill and Settings show the
	// answer.
	@objc func checkForUpdates(_: Any?) {
		Task { await updater.check() }
	}

	@objc func newTab(_ sender: Any?) { newTabInActiveProfile() }
	@objc func closeSelectedTab(_ sender: Any?) { closeActiveTab() }
	@objc func reopenLastClosedTab(_ sender: Any?) { reopenClosedTab() }
	@objc func findInPage(_ sender: Any?) { showFind() }
	@objc func reloadPage(_ sender: Any?) { reload() }
	@objc func zoomInPage(_ sender: Any?) { zoomIn() }
	@objc func zoomOutPage(_ sender: Any?) { zoomOut() }
	@objc func actualSize(_ sender: Any?) { resetZoom() }
	@objc func goBackPage(_ sender: Any?) { goBack() }
	@objc func goForwardPage(_ sender: Any?) { goForward() }
	@objc func focusAddressBar(_ sender: Any?) { state.focusAddressField() }
	@objc func showSettings(_ sender: Any?) { model.page = .settings }
	@objc func printActivePage(_ sender: Any?) { printPage() }
	@objc func toggleDeveloperTools(_ sender: Any?) { toggleDevTools() }

	@objc func selectTabByNumber(_ sender: NSMenuItem) {
		selectTab(atIndex: sender.tag - 1)
	}

	@objc func toggleFullScreenWindow(_ sender: Any?) {
		browserWindow.window.toggleFullScreen(sender)
	}

	@objc func nextTabMRU(_ sender: Any?) {
		state.switcher.visible ? cycleSwitcher(backward: false) : openSwitcher(backward: false)
	}

	@objc func previousTabMRU(_ sender: Any?) {
		state.switcher.visible ? cycleSwitcher(backward: true) : openSwitcher(backward: true)
	}
}
