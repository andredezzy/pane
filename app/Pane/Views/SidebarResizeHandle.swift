import AppKit
import PaneKit
import SwiftUI

// The sidebar's edge, which resizes it as a split view's divider does: the
// column cursor over it, the width following the pointer while dragged (no
// animation: the pointer leads, DESIGN.md: Motion), and a double-click for
// the standard width. At either limit the cursor points the one way left.
// Accessibility sees a splitter, which steps by 10 points.
struct SidebarResizeHandle: NSViewRepresentable {
	let width: CGFloat
	let resize: (CGFloat) -> Void
	let end: () -> Void
	let reset: () -> Void

	func makeNSView(context: Context) -> HandleView {
		HandleView()
	}

	func updateNSView(_ view: HandleView, context: Context) {
		let limitChanged = view.atLimit != HandleView.limit(of: width)
		view.width = width
		view.resize = resize
		view.end = end
		view.reset = reset

		if limitChanged {
			view.window?.invalidateCursorRects(for: view)
		}
	}

	final class HandleView: NSView {
		var width: CGFloat = 0
		var resize: (CGFloat) -> Void = { _ in }
		var end: () -> Void = {}
		var reset: () -> Void = {}
		// Where the drag began: the pointer's x in the window, and the width.
		private var start: (x: CGFloat, width: CGFloat)?

		var atLimit: Int {
			Self.limit(of: width)
		}

		// -1 at the narrowest, 1 at the widest, else 0.
		static func limit(of width: CGFloat) -> Int {
			width <= SidebarWidth.range.lowerBound ? -1 : width >= SidebarWidth.range.upperBound ? 1 : 0
		}

		private var cursor: NSCursor {
			if #available(macOS 15, *) {
				switch atLimit {
				case -1: return .columnResize(directions: .right)
				case 1: return .columnResize(directions: .left)
				default: return .columnResize
				}
			}

			return .resizeLeftRight
		}

		override var mouseDownCanMoveWindow: Bool {
			false
		}

		override func acceptsFirstMouse(for event: NSEvent?) -> Bool {
			true
		}

		override func resetCursorRects() {
			addCursorRect(bounds, cursor: cursor)
		}

		override func mouseDown(with event: NSEvent) {
			if event.clickCount == 2 {
				start = nil
				reset()
				return
			}

			start = (event.locationInWindow.x, width)
		}

		override func mouseDragged(with event: NSEvent) {
			guard let start else {
				return
			}

			resize(start.width + event.locationInWindow.x - start.x)
			// The pointer can outrun the edge; the cursor stays the edge's.
			cursor.set()
		}

		override func mouseUp(with event: NSEvent) {
			guard start != nil else {
				return
			}

			start = nil
			end()
		}

		// MARK: Accessibility

		override func isAccessibilityElement() -> Bool {
			true
		}

		override func accessibilityRole() -> NSAccessibility.Role? {
			.splitter
		}

		override func accessibilityLabel() -> String? {
			"Sidebar width"
		}

		override func accessibilityIdentifier() -> String {
			"pane.sidebar.resize"
		}

		override func accessibilityValue() -> Any? {
			NSNumber(value: Double(width))
		}

		override func accessibilityPerformIncrement() -> Bool {
			step(10)
		}

		override func accessibilityPerformDecrement() -> Bool {
			step(-10)
		}

		private func step(_ delta: CGFloat) -> Bool {
			resize(width + delta)
			end()
			return true
		}
	}
}
