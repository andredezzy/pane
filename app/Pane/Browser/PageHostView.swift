import AppKit
import PaneCore

// Holds the selected tab's page view, filling itself. The page sits in a
// rounded card, as in the Electron app's content panel. With DevTools docked,
// the DevTools page fills the card and the tab's page sits over it where the
// DevTools splitter puts it, as Chrome lays them out.
final class PageHostView: NSView {
	private weak var current: NSView?
	private weak var currentDevTools: NSView?
	// Where the page sits over DevTools (top-left origin); empty: hidden.
	private var pageBounds = NSRect.zero

	override var isFlipped: Bool {
		true
	}

	override init(frame: NSRect) {
		super.init(frame: frame)
		wantsLayer = true
		// The content panel's squircle (18), as a layer draws it: a circle of
		// the radius that follows it within a fifth of a pixel, so no mask
		// composites the page each frame. PageHostRepresentable keeps it in
		// step with the panel (square in full screen).
		layer?.cornerRadius = 18 * Squircle.circularEquivalent
		layer?.cornerCurve = .circular
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
	func show(_ page: WebContents?, devTools: WebContents? = nil) {
		let view = page?.nativeView
		let devToolsView = devTools?.nativeView

		if devToolsView !== currentDevTools {
			currentDevTools?.removeFromSuperview()
			currentDevTools = devToolsView

			if let devToolsView {
				devToolsView.frame = bounds
				devToolsView.autoresizingMask = [.width, .height]
				addSubview(devToolsView, positioned: .below, relativeTo: nil)
			}
		}

		if view !== current {
			current?.removeFromSuperview()
			current = view

			if let view {
				addSubview(view)
			}
		}

		pageBounds = devTools != nil ? page?.devToolsPageBounds ?? .zero : .zero
		layoutPage()
	}

	override func layout() {
		super.layout()
		layoutPage()
	}

	private func layoutPage() {
		guard let current else {
			return
		}

		if currentDevTools == nil {
			current.frame = bounds
			current.autoresizingMask = [.width, .height]
			current.isHidden = false
		} else {
			current.autoresizingMask = []
			current.frame = pageBounds
			current.isHidden = pageBounds.isEmpty
		}
	}
}
