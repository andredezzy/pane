import AppKit
import PaneKit
import SwiftUI

// Menu and keyboard actions, and the app-wide theme.
extension BrowserController {
	// MARK: Theme (ADR 0001: one knob for the app, its menus, and pages)

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
	@objc func focusAddressBar(_ sender: Any?) { focusAddressField() }
	@objc func showSettings(_ sender: Any?) { showPage(.settings) }
	@objc func lockPane(_ sender: Any?) { lockNow() }
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

// The address field and the find field do what ⌘L, ⌘T, and ⌘F ask before those
// keys return. A key already in the queue comes next, ahead of any update of
// SwiftUI's or block on the main queue, so through those it went to the page or
// was added to the address. SwiftUI's TextField is an NSTextField, found by its
// prompt.
extension BrowserController {
	private func textField(prompting prompt: String) -> NSTextField? {
		// Not there yet (⌘T from Settings), its frame builds now rather than next.
		hostingView?.textField(prompting: prompt) ?? {
			hostingView?.layoutSubtreeIfNeeded()
			return hostingView?.textField(prompting: prompt)
		}()
	}

	private var addressEditor: NSTextView? {
		guard
			let editor = browserWindow.window.firstResponder as? NSTextView,
			(editor.delegate as? NSTextField)?.placeholderAttributedString?.string == ToolbarView.addressPrompt
		else {
			return nil
		}

		return editor
	}

	var isEditingAddress: Bool {
		addressEditor != nil
	}

	// All of it selected, to type over. Already there, its focus won't change:
	// ⌘L still selects the address.
	func focusAddressField() {
		Log.interaction("Focus address field")

		if !isEditingAddress, let field = textField(prompting: ToolbarView.addressPrompt) {
			browserWindow.window.makeFirstResponder(field)
		}

		state.addressIsSelected = false
		selectAddressIfOwed()
	}

	// The field being edited takes it at once, selected. SwiftUI writes it again
	// as it updates, which leaves the caret at its end: selected again after that.
	func showAddress(_ url: String?) {
		state.address = url ?? ""
		state.shownAddress = state.address
		state.addressTabID = model.activeTabID

		if let editor = addressEditor {
			editor.string = state.address
			editor.selectAll(nil)
			state.addressIsSelected = false
			selectAddressNextTurn()
		}
	}

	func endEditingAddress() {
		if isEditingAddress {
			browserWindow.window.makeFirstResponder(nil)
		}
	}

	// A click or Tab gave it the focus, as SwiftUI reports: as the Electron app's
	// input.select() on focus, once a click has placed its caret.
	func addressFocusChanged(isFocused: Bool) {
		if isFocused {
			selectAddressNextTurn()
		} else if !isEditingAddress {
			state.addressIsSelected = false
		}
	}

	private func selectAddressNextTurn() {
		DispatchQueue.main.async { [weak self] in
			self?.selectAddressIfOwed()
		}
	}

	// A key that comes first has it selected ahead of that (KeyRouter).
	func selectAddressIfOwed() {
		if !state.addressIsSelected, let editor = addressEditor {
			editor.selectAll(nil)
			state.addressIsSelected = true
		}
	}

	func focusFindField() {
		if let field = textField(prompting: FindBar.prompt), field.currentEditor() == nil {
			browserWindow.window.makeFirstResponder(field)
		}
	}
}

extension NSView {
	fileprivate func textField(prompting prompt: String) -> NSTextField? {
		if let field = self as? NSTextField, field.placeholderAttributedString?.string == prompt {
			return field
		}

		return subviews.lazy.compactMap { $0.textField(prompting: prompt) }.first
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

		// Only a PIN locks Pane.
		if menuItem.action == #selector(lockPane(_:)) {
			return interactionAllowed && lock.state.pin != nil
		}

		return interactionAllowed || menuItem.action.map(Self.allowedWhileLocked.contains) == true
	}
}
