import AppKit

// The menu bar, with every shortcut of the Electron app (parity.md:
// Shortcuts). The engine builds none when hosted, so the app owns it.
@MainActor
enum MainMenu {
	static func build(target controller: BrowserController) -> NSMenu {
		let menu = NSMenu()
		menu.addItem(submenu: appMenu(controller))
		menu.addItem(submenu: fileMenu(controller))
		menu.addItem(submenu: editMenu())
		menu.addItem(submenu: viewMenu(controller))
		menu.addItem(submenu: navigateMenu(controller))
		menu.addItem(submenu: tabMenu(controller))
		menu.addItem(submenu: windowMenu(controller))

		return menu
	}

	private static func appMenu(_ controller: BrowserController) -> NSMenu {
		let menu = NSMenu(title: "Pane")
		menu.addItem(withTitle: "About Pane", action: #selector(NSApplication.orderFrontStandardAboutPanel(_:)), keyEquivalent: "")
		menu.addItem(.separator())
		menu.addItem(item("Check for Updates\u{2026}", #selector(BrowserController.checkForUpdates(_:)), "", target: controller))
		menu.addItem(.separator())
		menu.addItem(item("Settings…", #selector(BrowserController.showSettings(_:)), ",", target: controller))
		menu.addItem(.separator())
		menu.addItem(withTitle: "Hide Pane", action: #selector(NSApplication.hide(_:)), keyEquivalent: "h")
		menu.addItem(item("Hide Others", #selector(NSApplication.hideOtherApplications(_:)), "h", [.command, .option]))
		menu.addItem(withTitle: "Show All", action: #selector(NSApplication.unhideAllApplications(_:)), keyEquivalent: "")
		menu.addItem(.separator())
		menu.addItem(item("Quit Pane", #selector(BrowserController.confirmQuit(_:)), "q", target: controller))

		return menu
	}

	private static func fileMenu(_ controller: BrowserController) -> NSMenu {
		let menu = NSMenu(title: "File")
		menu.addItem(item("New Tab", #selector(BrowserController.newTab(_:)), "t", target: controller))
		menu.addItem(item("Close Tab", #selector(BrowserController.closeSelectedTab(_:)), "w", target: controller))
		// An uppercase key implies Shift, as Interface Builder stores ⇧⌘T: a
		// typed ⇧⌘T arrives as "T".
		menu.addItem(item("Reopen Closed Tab", #selector(BrowserController.reopenLastClosedTab(_:)), "T", target: controller))
		menu.addItem(.separator())
		menu.addItem(item("Print…", #selector(BrowserController.printActivePage(_:)), "p", target: controller))

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

	private static func viewMenu(_ controller: BrowserController) -> NSMenu {
		let menu = NSMenu(title: "View")
		menu.addItem(item("Find in Page", #selector(BrowserController.findInPage(_:)), "f", target: controller))
		menu.addItem(.separator())
		menu.addItem(item("Reload", #selector(BrowserController.reloadPage(_:)), "r", target: controller))
		menu.addItem(.separator())
		menu.addItem(item("Actual Size", #selector(BrowserController.actualSize(_:)), "0", target: controller))
		menu.addItem(item("Zoom In", #selector(BrowserController.zoomInPage(_:)), "+", target: controller))
		menu.addItem(item("Zoom In", #selector(BrowserController.zoomInPage(_:)), "=", target: controller, alternate: true))
		menu.addItem(item("Zoom Out", #selector(BrowserController.zoomOutPage(_:)), "-", target: controller))
		menu.addItem(.separator())
		menu.addItem(item("Enter Full Screen", #selector(BrowserController.toggleFullScreenWindow(_:)), "f", [.command, .control], target: controller))
		menu.addItem(.separator())
		menu.addItem(item("Developer Tools", #selector(BrowserController.toggleDeveloperTools(_:)), "I", target: controller))

		return menu
	}

	private static func navigateMenu(_ controller: BrowserController) -> NSMenu {
		let menu = NSMenu(title: "Navigate")
		menu.addItem(item("Back", #selector(BrowserController.goBackPage(_:)), "[", target: controller))
		menu.addItem(item("Forward", #selector(BrowserController.goForwardPage(_:)), "]", target: controller))
		menu.addItem(.separator())
		menu.addItem(item("Focus Address Bar", #selector(BrowserController.focusAddressBar(_:)), "l", target: controller))

		return menu
	}

	private static func tabMenu(_ controller: BrowserController) -> NSMenu {
		let menu = NSMenu(title: "Tab")
		// Shown for discoverability; KeyRouter handles the keys themselves.
		menu.addItem(item("Next Tab (Recent)", #selector(BrowserController.nextTabMRU(_:)), "\t", [.control], target: controller))
		menu.addItem(item("Previous Tab (Recent)", #selector(BrowserController.previousTabMRU(_:)), "\t", [.control, .shift], target: controller))
		menu.addItem(.separator())

		for number in 1 ... 9 {
			let tabItem = item("Tab \(number)", #selector(BrowserController.selectTabByNumber(_:)), "\(number)", target: controller)
			tabItem.tag = number
			menu.addItem(tabItem)
		}

		return menu
	}

	private static func windowMenu(_ controller: BrowserController) -> NSMenu {
		let menu = NSMenu(title: "Window")
		menu.addItem(withTitle: "Minimize", action: #selector(NSWindow.performMiniaturize(_:)), keyEquivalent: "m")
		menu.addItem(withTitle: "Zoom", action: #selector(NSWindow.performZoom(_:)), keyEquivalent: "")
		menu.addItem(.separator())
		// As Chrome's Window › Extensions. No key: pages use ⇧⌘E (Docs centers
		// text with it), and a menu's key would take it from them.
		menu.addItem(item("Extensions", #selector(BrowserController.showExtensionsPage(_:)), "", target: controller))
		NSApp.windowsMenu = menu

		return menu
	}

	// The title of the menu bar's item `event`, a key down, is the key of,
	// without its ellipsis ("New Tab", "Settings"): as the engine finds it
	// (MenuHasKeyEquivalent), where an uppercase key implies Shift.
	static func itemTitle(for event: NSEvent, in menu: NSMenu? = NSApp.mainMenu) -> String? {
		let mask: NSEvent.ModifierFlags = [.command, .option, .control, .shift]
		let flags = event.modifierFlags.intersection(mask)
		let key = event.charactersIgnoringModifiers?.lowercased()

		for item in menu?.items ?? [] {
			if let submenu = item.submenu {
				if let title = itemTitle(for: event, in: submenu) {
					return title
				}

				continue
			}

			var itemFlags = item.keyEquivalentModifierMask.intersection(mask)

			if item.keyEquivalent != item.keyEquivalent.lowercased() {
				itemFlags.insert(.shift)
			}

			if !item.keyEquivalent.isEmpty, itemFlags == flags, item.keyEquivalent.lowercased() == key {
				return item.title.hasSuffix("…") ? String(item.title.dropLast()) : item.title
			}
		}

		return nil
	}

	private static func item(
		_ title: String,
		_ action: Selector,
		_ key: String,
		_ modifiers: NSEvent.ModifierFlags = .command,
		target: AnyObject? = nil,
		alternate: Bool = false
	) -> NSMenuItem {
		let item = NSMenuItem(title: title, action: action, keyEquivalent: key)
		item.keyEquivalentModifierMask = modifiers
		item.target = target
		item.isHidden = alternate
		item.allowsKeyEquivalentWhenHidden = alternate

		return item
	}
}

private extension NSMenu {
	func addItem(submenu: NSMenu) {
		let item = NSMenuItem(title: submenu.title, action: nil, keyEquivalent: "")
		item.submenu = submenu
		addItem(item)
	}
}
