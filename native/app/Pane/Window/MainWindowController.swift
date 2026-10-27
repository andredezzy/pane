import AppKit
import PaneCore
import SwiftUI

// Puts the app's UI into the engine's window: a hidden title bar with the
// traffic lights at (12, 12), in the sidebar's header row, as in the Electron
// app (1280 × 800, at least 900 × 600).
@MainActor
final class MainWindowController {
	let controller: BrowserController

	private static let trafficLightOrigin = NSPoint(x: 12, y: 12)
	private var observers: [NSObjectProtocol] = []
	// The title bar's views whose frames are watched (watchTitleBar).
	private var watchedViews: Set<ObjectIdentifier> = []
	// While the buttons are being placed: the frame changes this makes are
	// its own, not AppKit's.
	private var placing = false
	// AppKit's distance between the buttons, read once, before any moves.
	private var buttonSpacing: CGFloat?
	// From entering full screen until it is over, the title bar is AppKit's.
	private var inFullScreen = false

	init(controller: BrowserController) {
		self.controller = controller

		let window = controller.browserWindow.window
		window.title = "Pane"
		window.titlebarAppearsTransparent = true
		window.titleVisibility = .hidden
		window.styleMask.insert(.fullSizeContentView)
		window.backgroundColor = NSColor(name: nil) { appearance in
			NSColor(rgb: Palette.isDark(appearance) ? 0x0A0A0C : 0xFAFAFA)
		}
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

		// AppKit puts the buttons back whenever it lays out the title bar.
		let center = NotificationCenter.default
		observers.append(center.addObserver(forName: NSWindow.willEnterFullScreenNotification, object: window, queue: .main) { [weak self] _ in
			MainActor.assumeIsolated { self?.inFullScreen = true }
		})
		observers.append(center.addObserver(forName: NSWindow.didExitFullScreenNotification, object: window, queue: .main) { [weak self] _ in
			MainActor.assumeIsolated { self?.inFullScreen = false }
		})
		for name in [
			NSWindow.didResizeNotification,
			NSWindow.didEndLiveResizeNotification,
			NSWindow.didExitFullScreenNotification,
			NSWindow.didBecomeKeyNotification,
			NSWindow.didResignKeyNotification,
		] {
			observers.append(center.addObserver(forName: name, object: window, queue: .main) { [weak self] _ in
				MainActor.assumeIsolated {
					self?.watchTitleBar()
					self?.placeTrafficLights()
				}
			})
		}

		watchTitleBar()
		placeTrafficLights()
	}

	// AppKit also lays the title bar out for a new title (the page's), with
	// no notification of its own, and the buttons showed at its spot until
	// the next resize or key change. Each button and their container report
	// their frames changing, synchronously (no queue), so the buttons go back
	// within the same layout pass, before it is drawn. Full screen can
	// rebuild the title bar, so the views are looked up again after it.
	private func watchTitleBar() {
		let window = controller.browserWindow.window

		guard
			let close = window.standardWindowButton(.closeButton),
			let miniaturize = window.standardWindowButton(.miniaturizeButton),
			let zoom = window.standardWindowButton(.zoomButton),
			let container = close.superview?.superview
		else {
			return
		}

		for view in [close, miniaturize, zoom, container] where !watchedViews.contains(ObjectIdentifier(view)) {
			watchedViews.insert(ObjectIdentifier(view))
			view.postsFrameChangedNotifications = true
			observers.append(NotificationCenter.default.addObserver(forName: NSView.frameDidChangeNotification, object: view, queue: nil) { [weak self] _ in
				MainActor.assumeIsolated { self?.placeTrafficLights() }
			})
		}
	}

	func showWindow() {
		let window = controller.browserWindow.window

		if !window.isVisible {
			window.center()
		}

		window.makeKeyAndOrderFront(nil)
		placeTrafficLights()
	}

	// The title bar container grows to the buttons plus the margin above and
	// below them, and the buttons sit in from the left edge.
	private func placeTrafficLights() {
		let window = controller.browserWindow.window

		guard
			!inFullScreen,
			!window.styleMask.contains(.fullScreen),
			let close = window.standardWindowButton(.closeButton),
			let miniaturize = window.standardWindowButton(.miniaturizeButton),
			let zoom = window.standardWindowButton(.zoomButton),
			let container = close.superview?.superview,
			!placing
		else {
			return
		}

		placing = true
		defer { placing = false }

		let spacing = buttonSpacing ?? miniaturize.frame.minX - close.frame.minX
		buttonSpacing = spacing
		let origin = Self.trafficLightOrigin
		let height = close.frame.height + origin.y * 2
		var frame = container.frame
		frame.size.height = height
		frame.origin.y = window.frame.height - height

		if container.frame != frame {
			container.frame = frame
		}

		for (index, button) in [close, miniaturize, zoom].enumerated() {
			let point = NSPoint(x: origin.x + CGFloat(index) * spacing, y: (height - button.frame.height) / 2)

			if button.frame.origin != point {
				button.setFrameOrigin(point)
			}
		}
	}
}
