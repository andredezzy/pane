import AppKit
import PaneCore
import SwiftUI

// The browser window: the sidebar on the left, the active page on the right.
final class MainWindowController: NSWindowController, NSWindowDelegate, WebContentsDelegate {
	private let context: BrowserContext
	private let webContents: WebContents
	private let contentView = NSView()

	init(context: BrowserContext) {
		self.context = context
		webContents = context.createWebContents()

		let window = NSWindow(
			contentRect: NSRect(x: 0, y: 0, width: 1280, height: 820),
			styleMask: [.titled, .closable, .miniaturizable, .resizable, .fullSizeContentView],
			backing: .buffered,
			defer: false
		)
		window.title = "Pane"
		window.titlebarAppearsTransparent = true
		window.contentMinSize = NSSize(width: 800, height: 500)

		super.init(window: window)

		window.delegate = self
		webContents.delegate = self

		// A content view controller resizes the window to its fitting size,
		// which is tiny for a split view, so the size is set after it. The
		// saved frame (if any) is restored last.
		window.contentViewController = makeSplitViewController()
		window.contentView?.wantsLayer = true
		window.setContentSize(NSSize(width: 1280, height: 820))
		window.center()
		window.setFrameAutosaveName("PaneMainWindow")
	}

	@available(*, unavailable)
	required init?(coder: NSCoder) {
		fatalError("init(coder:) is not supported")
	}

	func load(_ url: URL) {
		webContents.load(url)
	}

	private func makeSplitViewController() -> NSSplitViewController {
		let sidebar = NSHostingController(rootView: SidebarView())
		sidebar.view.setAccessibilityIdentifier("pane.sidebar")

		// Chromium draws pages into CALayers, so the views around them must be
		// layer-backed, as they are in Chrome's own windows.
		contentView.wantsLayer = true

		let page = NSViewController()
		page.view = contentView
		contentView.setAccessibilityIdentifier("pane.content")
		contentView.setAccessibilityElement(true)
		contentView.setAccessibilityRole(.group)
		contentView.setAccessibilityLabel("Web content")

		let pageView = webContents.nativeView
		pageView.frame = contentView.bounds
		pageView.autoresizingMask = [.width, .height]
		contentView.addSubview(pageView)
		webContents.isVisible = true

		let sidebarItem = NSSplitViewItem(sidebarWithViewController: sidebar)
		sidebarItem.minimumThickness = 220
		sidebarItem.maximumThickness = 360

		let split = NSSplitViewController()
		split.addSplitViewItem(sidebarItem)
		split.addSplitViewItem(NSSplitViewItem(viewController: page))

		return split
	}

	// MARK: WebContentsDelegate

	func webContents(_ webContents: WebContents, didChangeTitle title: String) {
		window?.title = title.isEmpty ? "Pane" : title
	}
}
