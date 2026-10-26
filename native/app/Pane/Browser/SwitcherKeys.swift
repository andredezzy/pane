import AppKit

// Ctrl+Tab and Ctrl+Shift+Tab step through the switcher while Control is
// held; releasing Control picks the highlighted tab, Escape cancels. Escape
// also leaves a page's fullscreen. A local event monitor sees the keys before
// any view, a page included.
@MainActor
final class SwitcherKeys {
	private let controller: BrowserController
	private var monitor: Any?

	init(controller: BrowserController) {
		self.controller = controller
		monitor = NSEvent.addLocalMonitorForEvents(matching: [.keyDown, .flagsChanged]) { [weak self] event in
			// Event monitors run on the main thread.
			nonisolated(unsafe) let event = event
			let consumed = MainActor.assumeIsolated { self?.consumes(event) ?? false }

			return consumed ? nil : event
		}
	}

	// Whether `event` drove the switcher and must not reach the window.
	private func consumes(_ event: NSEvent) -> Bool {
		let state = controller.state

		switch event.type {
		case .keyDown where event.keyCode == 48 && event.modifierFlags.contains(.control):
			let backward = event.modifierFlags.contains(.shift)

			if state.switcher.visible {
				controller.cycleSwitcher(backward: backward)
			} else {
				controller.openSwitcher(backward: backward)
			}

			return true

		case .keyDown where event.keyCode == 53 && state.switcher.visible:
			controller.cancelSwitcher()

			return true

		// Esc leaves a page's fullscreen wherever focus is, as in Chrome.
		case .keyDown where event.keyCode == 53 && state.pageFullscreen:
			controller.activePage?.exitFullscreen()

			return true

		case .flagsChanged where state.switcher.visible && !event.modifierFlags.contains(.control):
			controller.commitSwitcher()

			return false

		default:
			return false
		}
	}
}
