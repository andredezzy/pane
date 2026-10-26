import AppKit
import PaneCore
import SwiftUI

// Puts the app's UI into the engine's window.
@MainActor
final class MainWindowController {
	let controller: BrowserController

	init(controller: BrowserController) {
		self.controller = controller

		let window = controller.browserWindow.window
		window.title = "Pane"
		window.titlebarAppearsTransparent = true
		window.titleVisibility = .hidden
		window.styleMask.insert(.fullSizeContentView)
		window.contentMinSize = NSSize(width: 800, height: 500)
		window.setFrameAutosaveName("PaneMainWindow")

		let hostView = controller.browserWindow.hostView
		let root = NSHostingView(rootView: RootView(controller: controller))
		root.frame = hostView.bounds
		root.autoresizingMask = [.width, .height]
		hostView.addSubview(root)
	}

	func showWindow() {
		let window = controller.browserWindow.window

		if !window.isVisible {
			window.center()
		}

		window.makeKeyAndOrderFront(nil)
	}
}
