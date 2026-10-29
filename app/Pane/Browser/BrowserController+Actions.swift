import AppKit
import PaneKit
import SwiftUI

// Menu and keyboard actions, and the app-wide theme.
extension BrowserController {
	// MARK: Theme (ADR 0001: one knob for the app, its menus, and pages)

	func setTheme(_ theme: Theme) {
		changeSettings { $0.theme = theme }
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
		guard interactionAllowed else {
			return
		}

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
	@objc func showSettings(_ sender: Any?) { showPage(.settings) }
	@objc func showExtensionsPage(_ sender: Any?) { extensions.showPage() }
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

// Quit Pane? ⌘Q asks first, as Aside does; ⌘Q again, Return, or Quit quits,
// and Esc, Cancel, or a click outside goes back. At the PIN screen nothing is
// open to lose, so it quits at once. While it shows it takes every key, as
// the PIN screen does, so none reaches the page (KeyRouter).
extension BrowserController {
	@objc func confirmQuit(_ sender: Any?) {
		if state.confirmingQuit || lock.entry != nil {
			quit()
			return
		}

		withAnimation(Motion.panel) {
			state.confirmingQuit = true
		}
	}

	func cancelQuit() {
		withAnimation(Motion.panel) {
			state.confirmingQuit = false
		}
	}

	func quit() {
		state.confirmingQuit = false
		NSApp.terminate(nil)
	}
}

// The App icon picker (ADR 0089): Settings' icon opens it over the window. A
// click on a sky applies it at once and the Dock follows; Done, Return, Esc,
// or a click outside closes it.
extension BrowserController {
	func openAppIconPicker() {
		withAnimation(Motion.panel) {
			state.choosingAppIcon = true
		}
	}

	func closeAppIconPicker() {
		withAnimation(Motion.panel) {
			state.choosingAppIcon = false
		}
	}
}

extension BrowserController: NSMenuItemValidation {
	// The controller's items that work behind the PIN lock. About, Hide, and
	// the Window menu's own are the application's and the window's, which
	// open nothing.
	private static let allowedWhileLocked: Set<Selector> = [
		#selector(confirmQuit(_:)),
		#selector(checkForUpdates(_:)),
	]

	// In full screen the View menu's item reads Exit Full Screen, as AppKit's
	// own toggleFullScreen: item (the Electron app's role) does.
	func validateMenuItem(_ menuItem: NSMenuItem) -> Bool {
		if menuItem.action == #selector(toggleFullScreenWindow(_:)) {
			menuItem.title = browserWindow.window.styleMask.contains(.fullScreen) ? "Exit Full Screen" : "Enter Full Screen"
		}

		return interactionAllowed || menuItem.action.map(Self.allowedWhileLocked.contains) == true
	}
}
