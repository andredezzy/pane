import AppKit
import PaneCore
import SwiftUI

// The browser window: the engine's window, with the app's UI in its host view
// — the sidebar on the left, the selected tab on the right.
@MainActor
final class MainWindowController: NSObject, BrowserWindowDelegate, WebContentsDelegate {
	let browserWindow: BrowserWindow
	private let context: BrowserContext
	private let contentView = NSView()
	private var tab: WebContents?

	init(context: BrowserContext) {
		self.context = context
		browserWindow = BrowserWindow(contentRect: NSRect(x: 0, y: 0, width: 1280, height: 820))
		super.init()

		browserWindow.delegate = self
		browserWindow.activeContext = context

		let window = browserWindow.window
		window.title = "Pane"
		window.titlebarAppearsTransparent = true
		window.styleMask.insert(.fullSizeContentView)
		window.contentMinSize = NSSize(width: 800, height: 500)
		window.center()

		install(in: browserWindow.hostView)
	}

	func showWindow() {
		browserWindow.window.makeKeyAndOrderFront(nil)
	}

	func load(_ url: URL) {
		tab = browserWindow.openTab(url: url, context: context, foreground: true)
		tab?.delegate = self
		show(tab)
	}

	private func install(in hostView: NSView) {
		let split = NSSplitView(frame: hostView.bounds)
		split.isVertical = true
		split.dividerStyle = .thin
		split.autoresizingMask = [.width, .height]

		let sidebar = NSHostingView(rootView: SidebarView())
		sidebar.setAccessibilityIdentifier("pane.sidebar")
		sidebar.frame = NSRect(x: 0, y: 0, width: 220, height: hostView.bounds.height)

		contentView.wantsLayer = true
		contentView.setAccessibilityIdentifier("pane.content")
		contentView.setAccessibilityElement(true)
		contentView.setAccessibilityRole(.group)
		contentView.setAccessibilityLabel("Web content")

		split.addArrangedSubview(sidebar)
		split.addArrangedSubview(contentView)
		hostView.addSubview(split)
		split.setPosition(220, ofDividerAt: 0)
	}

	private func show(_ tab: WebContents?) {
		guard let tab else { return }
		let pageView = tab.nativeView
		pageView.frame = contentView.bounds
		pageView.autoresizingMask = [.width, .height]
		contentView.addSubview(pageView)
		tab.isVisible = true
	}

	// MARK: WebContentsDelegate

	func webContents(_ webContents: WebContents, didChangeTitle title: String) {
		browserWindow.window.title = title.isEmpty ? "Pane" : title
	}
}
