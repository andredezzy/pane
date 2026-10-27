import AppKit
import ObjectiveC

// Where AppKit puts the traffic lights: 12 points in from the left, centered
// in a 38-point title bar (12 above and below the 14-point buttons), in the
// sidebar's header row. Chrome's own browser window sets them the same way
// (BrowserWindowFrame in browser_native_widget_window_mac.mm): its frame view
// answers NSThemeFrame's `_titlebarHeight`, `_minXTitlebarWidgetInset`, and
// `_shouldCenterTrafficLights`, so every layout AppKit does, a new title's
// included, lays them out there. The engine's window has the plain titled
// frame, so this window's frame view takes a subclass of it that answers
// them, as KVO swaps an object's class. Full screen keeps AppKit's own.
@MainActor
enum TitleBar {
	static let height: CGFloat = 38
	static let buttonInset: CGFloat = 12

	static func install(on window: NSWindow) {
		guard let frameView = window.contentView?.superview else {
			return
		}

		let base: AnyClass = type(of: frameView)
		let name = "Pane_\(NSStringFromClass(base))"
		let subclass: AnyClass

		if let existing = NSClassFromString(name) {
			subclass = existing
		} else {
			guard let created = objc_allocateClassPair(base, name, 0) else {
				return
			}

			override(created, base, "_titlebarHeight", height)
			override(created, base, "_minXTitlebarWidgetInset", buttonInset)

			let center: @convention(block) (NSView) -> Bool = { _ in true }
			class_addMethod(created, NSSelectorFromString("_shouldCenterTrafficLights"), imp_implementationWithBlock(center), "B@:")

			objc_registerClassPair(created)
			subclass = created
		}

		object_setClass(frameView, subclass)
		frameView.needsLayout = true
		frameView.layoutSubtreeIfNeeded()
	}

	// A CGFloat getter that answers `value` outside full screen and the
	// frame's own answer in it.
	private static func override(_ subclass: AnyClass, _ base: AnyClass, _ name: String, _ value: CGFloat) {
		let selector = NSSelectorFromString(name)

		guard let original = class_getMethodImplementation(base, selector) else {
			return
		}

		typealias Getter = @convention(c) (NSView, Selector) -> CGFloat
		let inherited = unsafeBitCast(original, to: Getter.self)

		let block: @convention(block) (NSView) -> CGFloat = { view in
			view.window?.styleMask.contains(.fullScreen) == true ? inherited(view, selector) : value
		}

		class_addMethod(subclass, selector, imp_implementationWithBlock(block), "d@:")
	}
}
