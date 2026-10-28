import AppKit
import PaneCore

// Shows an extension action's popup page in a popover under its toolbar
// button, sized to the page; closing either one closes the other. Observed:
// the button stays lit while its popup shows.
@MainActor
@Observable
final class ExtensionPopover: NSObject, NSPopoverDelegate, ExtensionPopupDelegate {
	private(set) var popup: ExtensionPopup?
	@ObservationIgnored private var popover: NSPopover?

	// The popover's own window, while it shows.
	var window: NSWindow? {
		popover?.contentViewController?.view.window
	}

	func show(_ popup: ExtensionPopup, relativeTo rect: NSRect, of view: NSView) {
		close()

		// The page sits on an opaque canvas, white as a page's in Chrome.
		// Straight on the popover's material, the page would be blended with
		// it (vibrancy), darkening a dark popup to near black.
		let container = NSView(frame: NSRect(origin: .zero, size: Self.fit(popup.preferredSize)))
		// Under the dark appearance (the Dark theme) the popup's page never
		// paints, and the popover shows only this canvas. The page's own color
		// scheme follows the theme app-wide, so only its view stays aqua; the
		// popover around it stays dark.
		container.appearance = NSAppearance(named: .aqua)
		container.wantsLayer = true
		container.layer?.backgroundColor = NSColor.white.cgColor
		let page = popup.nativeView
		page.frame = container.bounds
		page.autoresizingMask = [.width, .height]
		container.addSubview(page)

		let controller = NSViewController()
		controller.view = container

		let popover = NSPopover()
		popover.behavior = .transient
		popover.animates = !Motion.isReduced
		popover.contentViewController = controller
		popover.contentSize = container.frame.size
		popover.delegate = self

		self.popup = popup
		self.popover = popover
		popup.delegate = self
		popover.show(relativeTo: rect, of: view, preferredEdge: .minY)
	}

	func close() {
		popover?.close()
		finish()
	}

	private func finish() {
		popover = nil

		if let popup {
			self.popup = nil
			popup.close()
		}
	}

	// Chrome's popup limits; the page sizes itself within them.
	private static func fit(_ size: NSSize) -> NSSize {
		NSSize(width: min(max(size.width, 25), 800), height: min(max(size.height, 25), 600))
	}

	// MARK: NSPopoverDelegate

	func popoverDidClose(_ notification: Notification) {
		finish()
	}

	// MARK: ExtensionPopupDelegate

	func extensionPopup(_ popup: ExtensionPopup, didResize size: NSSize) {
		popover?.contentSize = Self.fit(size)
	}

	func extensionPopupDidClose(_ popup: ExtensionPopup) {
		popover?.close()
		self.popup = nil
		popover = nil
	}
}
