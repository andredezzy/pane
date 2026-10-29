import AppKit
import PaneCore
import PaneKit

// The commands, by what they drive. Each goes through what the UI calls: the
// controller's intents and actions, the extensions', the lock's, the
// updater's. A group answers nil for a command that is not its own.
extension Automation {
	func run(_ command: AutomationCommand) throws -> Any? {
		if command.name == "state" {
			return nil
		}

		let groups = [profiles, tabs, pages, input, dialogs, extensions, security, updates, app]

		for group in groups {
			if case let .handled(result) = try group(command) {
				return result
			}
		}

		throw AutomationError("unknown command \(command.name)")
	}

	enum Outcome {
		case handled(Any?)
		case notMine
	}

	// MARK: Profiles

	private func profiles(_ command: AutomationCommand) throws -> Outcome {
		switch command.name {
		case "createProfile":
			// The profile sheet's Create, as filled in: { name, color?,
			// group?, spoof? }. Scenarios' profiles spoof nothing unless
			// asked, so pages see the engine's own values.
			var draft = ProfileDraft()
			draft.name = try command.string("name")
			draft.color = (command["color"] as? String).flatMap(ProfileColor.init) ?? .blue
			draft.group = command["group"] as? String ?? ""
			draft.spoofing = command["spoof"] as? Bool ?? false

			guard draft.validate() == nil else {
				throw AutomationError("createProfile needs a name")
			}

			return .handled(controller.createProfile(draft))

		case "deleteProfile":
			controller.deleteProfile(try command.string("profile"))

		case "moveProfile":
			controller.moveProfile(from: try command.int("from"), to: try command.int("to"))

		case "unloadProfile":
			// The header's click on an expanded profile.
			controller.collapseProfile(try command.string("profile"))

		case "openProfileSheet":
			// { profile? }: Edit profile for one, else New.
			controller.presentProfileSheet((command["profile"] as? String).map { .edit($0) } ?? .new)

		case "setProfileProxy":
			// { profile, proxy: { proxyType, host, port, username?, password? } | null }
			let proxy = try (command["proxy"] as? [String: Any]).map { try Self.decode(ProxyConfig.self, from: $0) }
			controller.updateProfile(try command.string("profile")) { $0.proxy = proxy }

		case "setProfileFingerprint":
			// { profile, fingerprint: Fingerprint (profiles.json's) | null }
			let fingerprint = try (command["fingerprint"] as? [String: Any]).map { try Self.decode(Fingerprint.self, from: $0) }
			controller.updateProfile(try command.string("profile")) { $0.fingerprint = fingerprint }

		case "setKeepLoaded":
			controller.setKeepLoaded(try command.string("profile"), command["keepLoaded"] as? Bool ?? true)

		case "setDownloadDirectory":
			try openContext(command).downloadDirectory = URL(fileURLWithPath: try command.string("path"), isDirectory: true)

		case "readCacheSize":
			// { profile }: the snapshot's profiles[].cacheSize has it once read.
			let profileID = try command.string("profile")
			let context = try openContext(command)
			cacheSizes[profileID] = nil

			Task { [weak self] in
				self?.cacheSizes[profileID] = await context.cacheSize()
			}

		case "readCookies":
			// { profile }: read in the background; the snapshot's
			// profiles[].cookies has them once read.
			let profileID = try command.string("profile")
			let context = try openContext(command)
			cookies[profileID] = nil

			Task { [weak self] in
				let all = await context.cookies()
				self?.cookies[profileID] = all.map(Self.describe)
			}

		default:
			return .notMine
		}

		return .handled(nil)
	}

	private func openContext(_ command: AutomationCommand) throws -> BrowserContext {
		let profileID = try command.string("profile")

		guard let context = controller.context(for: profileID) else {
			throw AutomationError("profile \(profileID) is not open")
		}

		return context
	}

	private static func decode<Value: Decodable>(_ type: Value.Type, from fields: [String: Any]) throws -> Value {
		try JSONDecoder().decode(type, from: JSONSerialization.data(withJSONObject: fields))
	}

	// MARK: Tabs

	private func tabs(_ command: AutomationCommand) throws -> Outcome {
		switch command.name {
		case "openTab":
			let profileID = try command.string("profile")
			controller.expandProfile(profileID)

			return .handled(controller.openTab(inProfile: profileID, url: (command["url"] as? String).flatMap(URL.init(string:))))

		case "moveTab":
			controller.moveTab(inProfile: try command.string("profile"), from: try command.int("from"), to: try command.int("to"))

		case "select":
			controller.select(tabID: try command.string("tab"))

		case "closeTab":
			controller.closeTab(try command.string("tab"))

		case "reopenClosedTab":
			controller.reopenClosedTab()

		case "reloadCrashed":
			controller.reloadCrashed(try command.string("tab"))

		case "switcher":
			switch try command.string("action") {
			case "next": controller.nextTabMRU(nil)
			case "previous": controller.previousTabMRU(nil)
			case "commit": controller.commitSwitcher()
			case "cancel": controller.cancelSwitcher()
			default: throw AutomationError("unknown switcher action")
			}

		default:
			return .notMine
		}

		return .handled(nil)
	}

	// MARK: The selected page

	private func pages(_ command: AutomationCommand) throws -> Outcome {
		switch command.name {
		case "navigate":
			controller.navigate(to: try command.string("input"))

		case "back":
			controller.goBack()

		case "forward":
			controller.goForward()

		case "reload":
			controller.reload()

		case "stop":
			controller.stop()

		case "zoom":
			switch try command.string("direction") {
			case "in": controller.zoomIn()
			case "out": controller.zoomOut()
			case "reset": controller.resetZoom()
			default: throw AutomationError("unknown zoom direction")
			}

		case "find":
			controller.showFind()
			controller.find(try command.string("text"), forward: command["forward"] as? Bool ?? true)

		case "closeFind":
			controller.closeFind()

		case "toggleDevTools":
			controller.toggleDevTools()

		case "print":
			controller.printPage()

		case "showPage":
			// { page: "browser" | "settings" | "extensions" }: the sidebar's
			// Settings, the menu's Extensions, or back.
			switch try command.string("page") {
			case "browser": controller.showPage(.browser)
			case "settings": controller.showPage(.settings)
			case "extensions": controller.extensions.showPage()
			default: throw AutomationError("unknown page")
			}

		default:
			return .notMine
		}

		return .handled(nil)
	}

	// MARK: Input: real events, and the menu bar

	private func input(_ command: AutomationCommand) throws -> Outcome {
		let window = controller.browserWindow.window

		switch command.name {
		case "key":
			// Posted into the app's event queue, so it takes the real path:
			// event monitors, then the menu's key equivalents, then the window.
			try postKey(try command.string("key"), modifiers: command["modifiers"] as? [String] ?? [])

		case "releaseModifiers":
			post(flagsChanged: [])

		case "typeInPage":
			// A key typed into the selected page: the page takes focus and the
			// window delivers the key to it, as when the user types. (Key events
			// are the input Chromium waits on; a hung page leaves them unanswered.)
			guard let page = controller.activePage else {
				throw AutomationError("no page")
			}

			let key = try command.string("key")
			page.focus()

			for type in [NSEvent.EventType.keyDown, .keyUp] {
				if let event = keyEvent(type, keyCode: Self.keyCodes[key] ?? 0, characters: key) {
					window.sendEvent(event)
				}
			}

		case "menu":
			// A click on an item of the menu bar, { path: ["File", "New Tab"] }:
			// it runs only when the menu enables it, as for the mouse. Returns
			// whether it ran.
			let path = command["path"] as? [String] ?? []

			guard let (menu, index) = Self.menuItem(at: path) else {
				throw AutomationError("no menu item \(path.joined(separator: " › "))")
			}

			menu.update()

			guard menu.item(at: index)?.isEnabled == true else {
				return .handled(false)
			}

			menu.performActionForItem(at: index)

			return .handled(true)

		case "activate":
			// What a click on the window does first: the app comes forward and
			// its window becomes key. A click activates the app at once; the
			// cooperative activate() would wait for the frontmost app to yield.
			NSApp.activate(ignoringOtherApps: true)
			window.makeKeyAndOrderFront(nil)

		case "clickPage":
			// A real click on the selected page, at a point in its view (top-left
			// origin), posted to the window like the trackpad's.
			guard let page = controller.activePage else {
				throw AutomationError("clickPage needs a page")
			}

			let point = try command.point()
			let view = page.nativeView
			let location = view.convert(NSPoint(x: point.x, y: view.isFlipped ? point.y : view.bounds.height - point.y), to: nil)
			postClick(at: location, clicks: 1)

		case "clickWindow":
			// A real click at a point of the window (top-left origin), posted
			// like the trackpad's: it goes through the window's hit testing,
			// title bar included. A double-click is a second click with a
			// count of 2.
			postClick(at: windowPoint(try command.point()), clicks: command["clicks"] as? Int ?? 1)

		case "hitTest":
			// The class of the view a click at a point of the window
			// (top-left origin) lands on, title bar included.
			let view = window.contentView?.superview?.hitTest(windowPoint(try command.point()))

			return .handled(view.map { String(describing: type(of: $0)) } ?? NSNull())

		case "scrollWindow":
			// A trackpad scroll at a point of the window (top-left origin), by
			// `dy` points (negative moves the content up, as a swipe up does),
			// dispatched through the window to the view under the point.
			guard let dy = command["dy"] as? Double else {
				throw AutomationError("scrollWindow needs dy")
			}

			window.sendEvent(try scrollEvent(at: windowPoint(try command.point()), dy: dy))

		case "pointerWindow":
			// One phase of a drag at a point of the window (top-left origin):
			// down, drag, or up, posted to the event queue as the trackpad's
			// are.
			let types: [String: NSEvent.EventType] = ["down": .leftMouseDown, "drag": .leftMouseDragged, "up": .leftMouseUp]

			guard let type = types[try command.string("phase")] else {
				throw AutomationError("pointerWindow's phase is down, drag, or up")
			}

			postPointer(type, at: windowPoint(try command.point()))

		case "hoverWindow":
			// The pointer over a point of the window (top-left origin), as a
			// mouse move the window dispatches; the real pointer stays put.
			if let event = mouseEvent(.mouseMoved, at: windowPoint(try command.point())) {
				window.sendEvent(event)
			}

		case "dragWindow":
			// A real drag from one point of the window to another (top-left
			// origin), in `steps` moves, dispatched through the window as the
			// trackpad's are. Each move is timed from its dispatch to the
			// window's layout at the result, the work before the next frame.
			guard
				let from = command["from"] as? [Double], from.count == 2,
				let to = command["to"] as? [Double], to.count == 2
			else {
				throw AutomationError("dragWindow needs from and to")
			}

			let times = drag(from: NSPoint(x: from[0], y: from[1]), to: NSPoint(x: to[0], y: to[1]), steps: max(command["steps"] as? Int ?? 10, 1))

			return .handled(["stepMs": times, "pageWidth": controller.pageHost.frame.width] as [String: Any])

		default:
			return .notMine
		}

		return .handled(nil)
	}

	// MARK: Dialogs, panels, and menus

	private func dialogs(_ command: AutomationCommand) throws -> Outcome {
		switch command.name {
		case "clickSheetButton":
			try clickSheetButton(try command.string("title"), texts: command["texts"] as? [String] ?? [])

		case "answerFileChooser":
			let urls = command["cancel"] as? Bool == true ? nil : (command["paths"] as? [String] ?? []).map { URL(fileURLWithPath: $0) }

			if !controller.dialogs.answerPanel(with: urls) {
				throw AutomationError("no file chooser")
			}

		case "chooseContextMenuItem":
			let title = try command.string("title")

			guard let menu = contextMenu, let index = menu.items.firstIndex(where: { $0.title == title }) else {
				throw AutomationError("no context menu item \(title)")
			}

			menu.cancelTracking()
			menu.performActionForItem(at: index)

		case "clickModalButton":
			let title = try command.string("title")

			guard let modal = NSApp.modalWindow ?? controller.browserWindow.window.attachedSheet else {
				throw AutomationError("no modal window")
			}

			guard let button = Self.buttons(in: modal.contentView).first(where: { $0.title == title }) else {
				throw AutomationError("no button \(title) in the modal window")
			}

			button.performClick(nil)

		default:
			return .notMine
		}

		return .handled(nil)
	}

	// A dialog's button, as the user clicks it: an extension's popup, or a
	// page's dialog, its fields filled first.
	private func clickSheetButton(_ title: String, texts: [String]) throws {
		if let prompt = controller.extensions.prompt {
			guard [prompt.acceptTitle, prompt.cancelTitle].contains(title) else {
				throw AutomationError("no button \(title); buttons: \([prompt.acceptTitle, prompt.cancelTitle])")
			}

			controller.extensions.answerPrompt(accepted: title == prompt.acceptTitle)
			return
		}

		let dialogs = controller.dialogs

		guard let dialog = dialogs.dialog else {
			throw AutomationError("no dialog")
		}

		guard let index = dialog.buttons.firstIndex(of: title) else {
			throw AutomationError("no button \(title); buttons: \(dialog.buttons)")
		}

		dialogs.fill(texts)
		dialogs.press(index)
	}

	// MARK: Extensions

	private func extensions(_ command: AutomationCommand) throws -> Outcome {
		let extensions = controller.extensions

		switch command.name {
		case "installExtension":
			extensions.install(try command.string("id"), inProfile: try command.string("profile"))

		case "runExtensionAction":
			extensions.runAction(try command.string("id"))

		case "setExtensionEnabled":
			extensions.setEnabled(try command.string("id"), command["enabled"] as? Bool ?? true, inProfile: try command.string("profile"))

		case "setExtensionPinned":
			extensions.setPinned(try command.string("id"), command["pinned"] as? Bool ?? true, inProfile: try command.string("profile"))

		case "uninstallExtension":
			extensions.uninstall(try command.string("id"), inProfile: try command.string("profile"))

		case "updateExtensions":
			extensions.update(inProfile: try command.string("profile"))

		case "closeExtensionPopup":
			extensions.popover.close()

		case "extensionsMenu":
			// What the puzzle and the extensions menu's controls call, for a
			// locked screen, where they cannot be clicked.
			try pressExtensionsMenu(try command.string("control"), command)

		default:
			return .notMine
		}

		return .handled(nil)
	}

	private func pressExtensionsMenu(_ control: String, _ command: AutomationCommand) throws {
		let extensions = controller.extensions

		switch control {
		case "puzzle": extensions.toggleMenu()
		case "manage": extensions.showPage()
		case "webStore": extensions.visitWebStore()
		default:
			let id = try command.string("id")

			guard let item = extensions.active.first(where: { $0.id == id }) else {
				throw AutomationError("no extension \(id) in the active profile")
			}

			switch control {
			case "run": extensions.runFromMenu(id)
			case "pin": extensions.togglePinned(item)
			case "more": extensions.toggleMoreMenu(id, row: extensions.actionAnchors[ExtensionsController.menuAnchor] ?? .zero)
			// The more menu's rows.
			case "storePage": extensions.openStorePage(id)
			case "remove": extensions.confirmUninstall(item)
			case "manageExtension": extensions.showPage(extensionID: id)
			default: throw AutomationError("unknown extensions menu control \(control)")
			}
		}
	}

	// MARK: Security

	private func security(_ command: AutomationCommand) throws -> Outcome {
		let lock = controller.lock

		switch command.name {
		case "showPinScreen":
			// Settings' Set up, Change, or Remove PIN.
			switch try command.string("mode") {
			case "setup": lock.show(.setup)
			case "change": lock.show(.change)
			case "remove": lock.show(.remove)
			default: throw AutomationError("unknown PIN screen mode")
			}

		case "pinKeys":
			// { keys: "1234" }: the numpad's digits; "<" is Delete, ">" is
			// Continue, "^" is the back arrow.
			for key in try command.string("keys") {
				switch key {
				case "<": lock.backspace()
				case ">": lock.continueWithPin()
				case "^": lock.dismiss()
				default: lock.digit(key)
				}
			}

		default:
			return .notMine
		}

		return .handled(nil)
	}

	// MARK: Updates

	private func updates(_ command: AutomationCommand) throws -> Outcome {
		let updater = controller.updater

		switch command.name {
		case "checkUpdates":
			Task { await updater.check() }

		case "downloadUpdate":
			// The pill's click.
			updater.downloadUpdate()

		case "dismissUpdate":
			updater.dismissPill()

		default:
			return .notMine
		}

		return .handled(nil)
	}

	// MARK: The app: settings, files, quitting

	private func app(_ command: AutomationCommand) throws -> Outcome {
		switch command.name {
		case "theme":
			guard let theme = Theme(rawValue: try command.string("theme")) else {
				throw AutomationError("unknown theme")
			}

			controller.setTheme(theme)

		case "appIcon":
			guard let icon = AppIcon(rawValue: try command.string("appIcon")) else {
				throw AutomationError("unknown app icon")
			}

			controller.changeSettings { $0.appIcon = icon }

		case "appIconPicker":
			// { open }: Settings' icon, or the picker's Done.
			(command["open"] as? Bool ?? true) ? controller.openAppIconPicker() : controller.closeAppIconPicker()

		case "setSleepSettings":
			// { tabSleepAfterMinutes, profileUnloadAfterMinutes, cacheBudgetMB },
			// each a number or null (off).
			controller.changeSettings {
				$0.tabSleepAfterMinutes = command["tabSleepAfterMinutes"] as? Int
				$0.profileUnloadAfterMinutes = command["profileUnloadAfterMinutes"] as? Int
				$0.cacheBudgetMB = command["cacheBudgetMB"] as? Int
			}

		case "unlearnWord":
			// Undoes a scenario's Add to Dictionary in the system dictionary.
			NSSpellChecker.shared.unlearnWord(try command.string("word"))

		case "render":
			// Draws the window's app UI (not the pages, which the GPU process
			// composites) into a PNG, for checks while the screen is locked.
			try render(to: URL(fileURLWithPath: try command.string("path")))

		case "save":
			store.saveNow()

		case "quit":
			store.saveNow()
			NSApp.terminate(nil)

		default:
			return .notMine
		}

		return .handled(nil)
	}

	// The menu holding the item at `path` in the menu bar, and its index.
	private static func menuItem(at path: [String]) -> (NSMenu, Int)? {
		guard let title = path.last, var menu = NSApp.mainMenu else {
			return nil
		}

		for name in path.dropLast() {
			guard let submenu = menu.items.first(where: { $0.title == name })?.submenu else {
				return nil
			}

			menu = submenu
		}

		let index = menu.indexOfItem(withTitle: title)

		return index < 0 ? nil : (menu, index)
	}

	private func render(to url: URL) throws {
		let view = controller.browserWindow.hostView
		view.layoutSubtreeIfNeeded()

		guard let bitmap = view.bitmapImageRepForCachingDisplay(in: view.bounds) else {
			throw AutomationError("cannot render")
		}

		view.cacheDisplay(in: view.bounds, to: bitmap)

		guard let png = bitmap.representation(using: .png, properties: [:]) else {
			throw AutomationError("cannot encode")
		}

		try png.write(to: url)
	}
}
