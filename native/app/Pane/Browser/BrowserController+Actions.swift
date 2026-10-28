import AppKit
import PaneKit
import SwiftUI

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
	@objc func showSettings(_ sender: Any?) { model.page = .settings }
	@objc func showExtensionsPage(_ sender: Any?) { showExtensions() }
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
// open to lose, so it quits at once.
extension BrowserController {
	@objc func confirmQuit(_ sender: Any?) {
		if state.confirmingQuit || lock.entry != nil {
			quit()
			return
		}

		quitKeyMonitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { [weak self] event in
			// Event monitors run on the main thread.
			nonisolated(unsafe) let event = event
			let consumed = MainActor.assumeIsolated { self?.quitPanelConsumes(event) ?? false }

			return consumed ? nil : event
		}

		withAnimation(Self.panelAnimation) {
			state.confirmingQuit = true
		}
	}

	func cancelQuit() {
		removeQuitKeyMonitor()

		withAnimation(Self.panelAnimation) {
			state.confirmingQuit = false
		}
	}

	func quit() {
		removeQuitKeyMonitor()
		state.confirmingQuit = false
		NSApp.terminate(nil)
	}

	private static var panelAnimation: Animation? {
		NSWorkspace.shared.accessibilityDisplayShouldReduceMotion ? nil : .easeOut(duration: 0.15)
	}

	// While Quit Pane? shows it takes every key, as the PIN screen does, so
	// none reaches the page: Esc goes back, Return quits, and ⌘Q goes on to
	// the menu, which quits.
	private func quitPanelConsumes(_ event: NSEvent) -> Bool {
		if event.isQuitShortcut {
			return false
		}

		switch event.keyCode {
		case 53:
			cancelQuit()
		case 36, 76:
			quit()
		default:
			break
		}

		return true
	}

	private func removeQuitKeyMonitor() {
		if let quitKeyMonitor {
			NSEvent.removeMonitor(quitKeyMonitor)
			self.quitKeyMonitor = nil
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
