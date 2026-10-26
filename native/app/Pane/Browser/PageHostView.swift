import AppKit
import PaneCore

// Holds the selected tab's page view, filling itself. The page sits in a
// rounded card, as in the Electron app's content panel.
final class PageHostView: NSView {
	private weak var current: NSView?

	override init(frame: NSRect) {
		super.init(frame: frame)
		wantsLayer = true
		layer?.cornerRadius = 10
		layer?.cornerCurve = .continuous
		layer?.masksToBounds = true
		setAccessibilityIdentifier("pane.content")
		setAccessibilityElement(true)
		setAccessibilityRole(.group)
		setAccessibilityLabel("Web content")
	}

	@available(*, unavailable)
	required init?(coder: NSCoder) {
		fatalError("init(coder:) is not supported")
	}

	@MainActor
	func show(_ page: WebContents?) {
		let view = page?.nativeView

		guard view !== current else {
			return
		}

		current?.removeFromSuperview()
		current = view

		if let view {
			view.frame = bounds
			view.autoresizingMask = [.width, .height]
			addSubview(view)
		}
	}
}
