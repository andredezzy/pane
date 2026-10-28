import AppKit
import ObjectiveC

// Where AppKit puts the traffic lights: 12 points in from the left, centered
// in a 38-point title bar (12 above and below the 14-point buttons), in the
// sidebar's header row. Chrome's own browser window sets them the same way
// (browser_native_widget_window_mac.mm): its window class answers
// `frameViewClassForStyleMask:` with a frame view, BrowserWindowFrame, that
// answers NSThemeFrame's `_titlebarHeight`, `_minXTitlebarWidgetInset`, and
// `_shouldCenterTrafficLights`, so every layout AppKit does lays them out
// there. The engine's windows are Chromium's plain NativeWidgetMacNSWindow,
// so before the engine makes the app's window, that class answers with a
// subclass of its titled frame view that does, for the app's window alone.
// AppKit makes the frame view anew whenever the window's style mask changes,
// as full screen's does, and each new one is the subclass too. Full screen
// keeps AppKit's own.
@MainActor
enum TitleBar {
	static let height: CGFloat = 38
	static let buttonInset: CGFloat = 12

	// The app's window: every other window keeps AppKit's title bar.
	private static weak var window: NSWindow?

	// Before the engine makes the window.
	static func prepare() {
		let selector = NSSelectorFromString("frameViewClassForStyleMask:")

		guard
			let windowClass = NSClassFromString("NativeWidgetMacNSWindow"),
			let titledFrame = NSClassFromString("NativeWidgetMacNSWindowTitledFrame"),
			let method = class_getClassMethod(windowClass, selector),
			let frame = subclass(of: titledFrame)
		else {
			return
		}

		typealias FrameClass = @convention(c) (AnyClass, Selector, UInt) -> AnyClass
		let inherited = unsafeBitCast(method_getImplementation(method), to: FrameClass.self)

		let answer: @convention(block) (AnyClass, UInt) -> AnyClass = { windowClass, styleMask in
			let chosen: AnyClass = inherited(windowClass, selector, styleMask)
			return chosen == titledFrame ? frame : chosen
		}

		method_setImplementation(method, imp_implementationWithBlock(answer))
	}

	// The engine has made the window. AppKit laid its title bar out as it
	// made it, so it is laid out again at once, as a new title would, before
	// the window is first drawn.
	static func install(on window: NSWindow) {
		self.window = window

		let selector = NSSelectorFromString("_tileTitlebarAndRedisplay:")

		guard let frameView = window.contentView?.superview, frameView.responds(to: selector) else {
			return
		}

		typealias Tile = @convention(c) (NSView, Selector, Bool) -> Void
		unsafeBitCast(frameView.method(for: selector), to: Tile.self)(frameView, selector, false)
	}

	// The titled frame view's subclass, made once.
	private static func subclass(of base: AnyClass) -> AnyClass? {
		let name = "Pane_\(NSStringFromClass(base))"

		if let existing = NSClassFromString(name) {
			return existing
		}

		guard let created = objc_allocateClassPair(base, name, 0) else {
			return nil
		}

		override(created, base, "_titlebarHeight", height)
		override(created, base, "_minXTitlebarWidgetInset", buttonInset)
		override(created, base, "_shouldCenterTrafficLights", true)
		objc_registerClassPair(created)

		return created
	}

	// A getter that answers `value` for the app's window outside full screen,
	// and the frame's own answer otherwise.
	private static func override(_ subclass: AnyClass, _ base: AnyClass, _ name: String, _ value: CGFloat) {
		let selector = NSSelectorFromString(name)

		guard let original = class_getMethodImplementation(base, selector) else {
			return
		}

		typealias Getter = @convention(c) (NSView, Selector) -> CGFloat
		let inherited = unsafeBitCast(original, to: Getter.self)

		let block: @convention(block) (NSView) -> CGFloat = { view in
			answers(for: view) ? value : inherited(view, selector)
		}

		class_addMethod(subclass, selector, imp_implementationWithBlock(block), "d@:")
	}

	private static func override(_ subclass: AnyClass, _ base: AnyClass, _ name: String, _ value: Bool) {
		let selector = NSSelectorFromString(name)

		guard let original = class_getMethodImplementation(base, selector) else {
			return
		}

		typealias Getter = @convention(c) (NSView, Selector) -> Bool
		let inherited = unsafeBitCast(original, to: Getter.self)

		let block: @convention(block) (NSView) -> Bool = { view in
			answers(for: view) ? value : inherited(view, selector)
		}

		class_addMethod(subclass, selector, imp_implementationWithBlock(block), "B@:")
	}

	private static func answers(for view: NSView) -> Bool {
		guard let window = view.window else {
			return false
		}

		return window === self.window && !window.styleMask.contains(.fullScreen)
	}
}
