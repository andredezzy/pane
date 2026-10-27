import AppKit
import SwiftUI

// Where the window moves, as the Electron app's drag regions did (its root
// and the lock screen's panel were `-webkit-app-region: drag`; controls, the
// sidebar's list, and the content panel were not): what lies under no
// control. The window is the engine's, whose title bar moves it only where
// Chrome's own views say so, so this view starts the drag itself, the way
// Chrome's frame view does (`performWindowDragWithEvent:`). A double-click
// does what the title bar's does (System Settings › Desktop & Dock).
struct WindowDragArea: NSViewRepresentable {
	func makeNSView(context: Context) -> NSView {
		WindowDragView()
	}

	func updateNSView(_ nsView: NSView, context: Context) {}
}

final class WindowDragView: NSView {
	override var mouseDownCanMoveWindow: Bool { true }

	override func mouseDown(with event: NSEvent) {
		guard let window else {
			return
		}

		guard event.clickCount == 2 else {
			window.performDrag(with: event)
			return
		}

		switch UserDefaults.standard.string(forKey: "AppleActionOnDoubleClick") {
		case "Minimize":
			window.performMiniaturize(nil)
		case "None":
			break
		default:
			window.performZoom(nil)
		}
	}
}
