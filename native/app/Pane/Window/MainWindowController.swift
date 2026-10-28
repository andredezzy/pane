import AppKit
import PaneCore
import SwiftUI

// Puts the app's UI into the engine's window: a hidden title bar with the
// traffic lights at (12, 12), in the sidebar's header row (TitleBar), as in
// the Electron app (1280 × 800, at least 900 × 600).
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
		window.backgroundColor = Palette.windowBackground
		window.contentMinSize = NSSize(width: 900, height: 600)
		window.isRestorable = false

		// 1280 × 800 the first time; then where the user left it.
		if UserDefaults.standard.string(forKey: "NSWindow Frame PaneMainWindow") == nil {
			window.setContentSize(NSSize(width: 1280, height: 800))
		}

		window.setFrameAutosaveName("PaneMainWindow")

		let hostView = controller.browserWindow.hostView
		let root = NSHostingView(rootView: RootView(controller: controller))
		root.frame = hostView.bounds
		root.autoresizingMask = [.width, .height]
		hostView.addSubview(root)
		controller.hostingView = root

		TitleBar.install(on: window)
	}

	func showWindow() {
		let window = controller.browserWindow.window

		if !window.isVisible {
			window.center()
		}

		window.makeKeyAndOrderFront(nil)
	}
}
