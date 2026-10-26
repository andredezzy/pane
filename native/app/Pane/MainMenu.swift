import AppKit

// The menu bar. The engine builds none when hosted, so the app owns it.
@MainActor
enum MainMenu {
	static func build() -> NSMenu {
		let menu = NSMenu()
		menu.addItem(submenu: appMenu())
		menu.addItem(submenu: editMenu())
		menu.addItem(submenu: windowMenu())

		return menu
	}

	private static func appMenu() -> NSMenu {
		let menu = NSMenu(title: "Pane")
		menu.addItem(withTitle: "About Pane", action: #selector(NSApplication.orderFrontStandardAboutPanel(_:)), keyEquivalent: "")
		menu.addItem(.separator())
		menu.addItem(withTitle: "Hide Pane", action: #selector(NSApplication.hide(_:)), keyEquivalent: "h")
		menu.addItem(withTitle: "Quit Pane", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")

		return menu
	}

	private static func editMenu() -> NSMenu {
		let menu = NSMenu(title: "Edit")
		menu.addItem(withTitle: "Undo", action: Selector(("undo:")), keyEquivalent: "z")
		menu.addItem(withTitle: "Redo", action: Selector(("redo:")), keyEquivalent: "Z")
		menu.addItem(.separator())
		menu.addItem(withTitle: "Cut", action: #selector(NSText.cut(_:)), keyEquivalent: "x")
		menu.addItem(withTitle: "Copy", action: #selector(NSText.copy(_:)), keyEquivalent: "c")
		menu.addItem(withTitle: "Paste", action: #selector(NSText.paste(_:)), keyEquivalent: "v")
		menu.addItem(withTitle: "Select All", action: #selector(NSText.selectAll(_:)), keyEquivalent: "a")

		return menu
	}

	private static func windowMenu() -> NSMenu {
		let menu = NSMenu(title: "Window")
		menu.addItem(withTitle: "Minimize", action: #selector(NSWindow.performMiniaturize(_:)), keyEquivalent: "m")
		menu.addItem(withTitle: "Zoom", action: #selector(NSWindow.performZoom(_:)), keyEquivalent: "")
		NSApp.windowsMenu = menu

		return menu
	}
}

private extension NSMenu {
	func addItem(submenu: NSMenu) {
		let item = NSMenuItem(title: submenu.title, action: nil, keyEquivalent: "")
		item.submenu = submenu
		addItem(item)
	}
}
