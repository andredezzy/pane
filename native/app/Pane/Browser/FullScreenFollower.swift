import AppKit

// The window's full screen follows a page's fullscreen, as Chrome's does: it
// enters with the page and leaves with it, unless the user had the window in
// full screen already, which stays. Leaving full screen takes the page out of
// its fullscreen too. AppKit ignores a toggle while the window is still moving
// in or out, so a page that leaves as the window enters (Esc at once) is
// followed when the move ends; the window stayed in full screen before.
@MainActor
final class FullScreenFollower {
	private unowned let controller: BrowserController
	// The window is in full screen for the page, not the user.
	private var isPages = false
	// The app toggled the window: the page is followed again as the move ends.
	private var toggled = false
	private var observers: [NSObjectProtocol] = []

	init(controller: BrowserController) {
		self.controller = controller

		for name in [NSWindow.didEnterFullScreenNotification, NSWindow.didExitFullScreenNotification] {
			observers.append(NotificationCenter.default.addObserver(
				forName: name,
				object: controller.browserWindow.window,
				queue: .main
			) { [weak self] notification in
				let entered = notification.name == NSWindow.didEnterFullScreenNotification
				MainActor.assumeIsolated { self?.moveDidEnd(entered: entered) }
			})
		}
	}

	// The page entered or left its fullscreen.
	func follow() {
		let window = controller.browserWindow.window
		let isFullScreen = window.styleMask.contains(.fullScreen)

		if controller.state.pageFullscreen {
			guard !isFullScreen else {
				return
			}

			isPages = true
		} else if !isFullScreen || !isPages {
			return
		}

		toggled = true
		window.toggleFullScreen(nil)
	}

	private func moveDidEnd(entered: Bool) {
		if !entered {
			isPages = false
		}

		if toggled {
			toggled = false

			// AppKit ignores a toggle from within the move's own end, as
			// Chromium's fullscreen controller knows: one turn later.
			DispatchQueue.main.async { [weak self] in
				self?.follow()
			}
		} else if !entered, controller.state.pageFullscreen {
			controller.activePage?.exitFullscreen()
		}
	}
}
