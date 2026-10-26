import AppKit
import PaneCore
import PaneKit

// A test-only command channel, on when PANE_AUTOMATION_DIR is set: the
// scenario runner drops JSON commands into `<dir>/in/<n>.json` and reads the
// result, with a snapshot of the app's state, from `<dir>/out/<n>.json`. It
// drives the same controller methods the UI calls, so scenarios exercise the
// app's paths, not test-only ones.
@MainActor
final class Automation {
	private let inbox: URL
	private let outbox: URL
	private let controller: BrowserController
	private var timer: Timer?
	private var downloads: [String: [String: Any]] = [:]
	// Each profile's cookies (names, domains, and paths; never values), as
	// last read with readCookies.
	private var cookies: [String: [[String: Any]]] = [:]
	// Each profile's HTTP cache size in bytes, as last read with
	// readCacheSize.
	private var cacheSizes: [String: Int64] = [:]
	private var observer: NSObjectProtocol?

	init(directory: URL, controller: BrowserController) {
		inbox = directory.appendingPathComponent("in", isDirectory: true)
		outbox = directory.appendingPathComponent("out", isDirectory: true)
		self.controller = controller

		try? FileManager.default.createDirectory(at: inbox, withIntermediateDirectories: true)
		try? FileManager.default.createDirectory(at: outbox, withIntermediateDirectories: true)

		observer = NotificationCenter.default.addObserver(forName: .paneDownloadDidUpdate, object: nil, queue: .main) { [weak self] note in
			// Posted on the main thread by BrowserController.
			nonisolated(unsafe) let download = note.object as? Download

			MainActor.assumeIsolated {
				if let download {
					self?.downloads[download.identifier] = Self.describe(download)
				}
			}
		}

		let timer = Timer(timeInterval: 0.1, repeats: true) { [weak self] _ in
			MainActor.assumeIsolated { self?.poll() }
		}
		RunLoop.main.add(timer, forMode: .common)
		self.timer = timer
	}

	private func poll() {
		let files = (try? FileManager.default.contentsOfDirectory(at: inbox, includingPropertiesForKeys: nil)) ?? []

		for file in files.filter({ $0.pathExtension == "json" }).sorted(by: { $0.lastPathComponent < $1.lastPathComponent }) {
			let data = try? Data(contentsOf: file)
			try? FileManager.default.removeItem(at: file)

			var reply: [String: Any]

			do {
				guard let data, let command = try JSONSerialization.jsonObject(with: data) as? [String: Any] else {
					throw AutomationError("unreadable command")
				}

				reply = ["ok": true, "result": try run(command) ?? NSNull()]
			} catch {
				reply = ["ok": false, "error": String(describing: error)]
			}

			reply["state"] = snapshot()
			write(reply, to: outbox.appendingPathComponent(file.lastPathComponent))
		}
	}

	private func write(_ object: [String: Any], to url: URL) {
		guard let data = try? JSONSerialization.data(withJSONObject: object, options: [.prettyPrinted, .sortedKeys]) else {
			return
		}

		let temporary = url.appendingPathExtension("tmp")
		try? data.write(to: temporary)
		_ = try? FileManager.default.replaceItemAt(url, withItemAt: temporary)
	}

	// MARK: Commands

	private func run(_ command: [String: Any]) throws -> Any? {
		let model = controller.model
		let name = command["command"] as? String ?? ""

		func string(_ key: String) throws -> String {
			guard let value = command[key] as? String else {
				throw AutomationError("\(name) needs \(key)")
			}

			return value
		}

		switch name {
		case "state":
			return nil

		case "createProfile":
			let color = (command["color"] as? String).flatMap(ProfileColor.init) ?? .blue
			let id = model.createProfile(name: try string("name"), color: color, group: command["group"] as? String)
			model.expandedProfileIDs.insert(id)

			return id

		case "deleteProfile":
			controller.deleteProfile(try string("profile"))

		case "openTab":
			let profileID = try string("profile")
			model.expandedProfileIDs.insert(profileID)

			return controller.openTab(inProfile: profileID, url: (command["url"] as? String).flatMap(URL.init(string:)))

		case "moveProfile":
			guard let from = command["from"] as? Int, let to = command["to"] as? Int else {
				throw AutomationError("moveProfile needs from and to")
			}

			model.moveProfile(from: from, to: to)

		case "moveTab":
			guard let from = command["from"] as? Int, let to = command["to"] as? Int else {
				throw AutomationError("moveTab needs from and to")
			}

			model.moveTab(inProfile: try string("profile"), from: from, to: to)

		case "select":
			controller.select(tabID: try string("tab"))

		case "closeTab":
			controller.closeTab(try string("tab"))

		case "reopenClosedTab":
			controller.reopenClosedTab()

		case "navigate":
			controller.navigate(to: try string("input"))

		case "back":
			controller.goBack()

		case "forward":
			controller.goForward()

		case "reload":
			controller.reload()

		case "stop":
			controller.stop()

		case "zoom":
			switch try string("direction") {
			case "in": controller.zoomIn()
			case "out": controller.zoomOut()
			case "reset": controller.resetZoom()
			default: throw AutomationError("unknown zoom direction")
			}

		case "find":
			controller.showFind()
			controller.find(try string("text"), forward: command["forward"] as? Bool ?? true)

		case "closeFind":
			controller.closeFind()

		case "theme":
			guard let theme = Theme(rawValue: try string("theme")) else {
				throw AutomationError("unknown theme")
			}

			controller.setTheme(theme)

		case "unloadProfile":
			let profileID = try string("profile")
			model.expandedProfileIDs.remove(profileID)
			controller.unloadProfile(profileID)

		case "setDownloadDirectory":
			guard let context = controller.context(for: try string("profile")) else {
				throw AutomationError("profile not open")
			}

			context.downloadDirectory = URL(fileURLWithPath: try string("path"), isDirectory: true)

		case "switcher":
			switch try string("action") {
			case "next": controller.nextTabMRU(nil)
			case "previous": controller.previousTabMRU(nil)
			case "commit": controller.commitSwitcher()
			case "cancel": controller.cancelSwitcher()
			default: throw AutomationError("unknown switcher action")
			}

		case "key":
			// Posted into the app's event queue, so it takes the real path:
			// event monitors, then the menu's key equivalents, then the window.
			try postKey(try string("key"), modifiers: command["modifiers"] as? [String] ?? [])

		case "clickPage":
			// A real click on the selected page, at a point in its view (top-left
			// origin), posted to the window like the trackpad's.
			guard
				let page = controller.activePage,
				let x = command["x"] as? Double,
				let y = command["y"] as? Double
			else {
				throw AutomationError("clickPage needs a page, x and y")
			}

			let view = page.nativeView
			let point = view.convert(NSPoint(x: x, y: view.isFlipped ? y : view.bounds.height - y), to: nil)
			let window = controller.browserWindow.window

			for type in [NSEvent.EventType.leftMouseDown, .leftMouseUp] {
				if let event = NSEvent.mouseEvent(
					with: type,
					location: point,
					modifierFlags: [],
					timestamp: ProcessInfo.processInfo.systemUptime,
					windowNumber: window.windowNumber,
					context: nil,
					eventNumber: 0,
					clickCount: 1,
					pressure: type == .leftMouseDown ? 1 : 0
				) {
					NSApp.postEvent(event, atStart: false)
				}
			}

		case "clickWindow":
			// A real click at a point of the window (top-left origin), posted
			// like the trackpad's: it goes through the window's hit testing,
			// title bar included.
			guard let x = command["x"] as? Double, let y = command["y"] as? Double else {
				throw AutomationError("clickWindow needs x and y")
			}

			let window = controller.browserWindow.window
			let point = NSPoint(x: x, y: window.frame.height - y)

			for type in [NSEvent.EventType.leftMouseDown, .leftMouseUp] {
				if let event = NSEvent.mouseEvent(
					with: type,
					location: point,
					modifierFlags: [],
					timestamp: ProcessInfo.processInfo.systemUptime,
					windowNumber: window.windowNumber,
					context: nil,
					eventNumber: 0,
					clickCount: 1,
					pressure: type == .leftMouseDown ? 1 : 0
				) {
					NSApp.postEvent(event, atStart: false)
				}
			}

		case "typeInPage":
			// A key typed into the selected page: the page takes focus and the
			// window delivers the key to it, as when the user types. (Key events
			// are the input Chromium waits on; a hung page leaves them unanswered.)
			guard let page = controller.activePage else {
				throw AutomationError("no page")
			}

			let key = try string("key")
			let window = controller.browserWindow.window
			page.focus()

			for type in [NSEvent.EventType.keyDown, .keyUp] {
				if let event = NSEvent.keyEvent(
					with: type,
					location: .zero,
					modifierFlags: [],
					timestamp: ProcessInfo.processInfo.systemUptime,
					windowNumber: window.windowNumber,
					context: nil,
					characters: key,
					charactersIgnoringModifiers: key,
					isARepeat: false,
					keyCode: Self.keyCodes[key] ?? 0
				) {
					window.sendEvent(event)
				}
			}

		case "releaseModifiers":
			post(flagsChanged: [])

		case "clickSheetButton":
			try clickSheetButton(try string("title"), texts: command["texts"] as? [String] ?? [])

		case "answerFileChooser":
			try answerFileChooser(paths: command["paths"] as? [String], cancel: command["cancel"] as? Bool ?? false)

		case "chooseContextMenuItem":
			let title = try string("title")

			guard
				let menu = controller.lastContextMenu,
				let index = menu.items.firstIndex(where: { $0.title == title })
			else {
				throw AutomationError("no context menu item \(title)")
			}

			menu.cancelTracking()
			menu.performActionForItem(at: index)

		case "clickModalButton":
			let title = try string("title")

			guard let modal = NSApp.modalWindow ?? controller.browserWindow.window.attachedSheet else {
				throw AutomationError("no modal window")
			}

			guard let button = Self.buttons(in: modal.contentView).first(where: { $0.title == title }) else {
				throw AutomationError("no button \(title) in the modal window")
			}

			button.performClick(nil)

		case "toggleDevTools":
			controller.toggleDevTools()

		case "print":
			controller.printPage()

		case "reloadCrashed":
			controller.reloadCrashed(try string("tab"))

		case "unlearnWord":
			// Undoes a scenario's Add to Dictionary in the system dictionary.
			NSSpellChecker.shared.unlearnWord(try string("word"))

		case "setProfileProxy":
			// { profile, proxy: { proxyType, host, port, username?, password? } | null }
			let profileID = try string("profile")
			var proxy: ProxyConfig?

			if let fields = command["proxy"] as? [String: Any] {
				let data = try JSONSerialization.data(withJSONObject: fields)
				proxy = try JSONDecoder().decode(ProxyConfig.self, from: data)
			}

			model.updateProfile(profileID) { $0.proxy = proxy }

		case "setProfileFingerprint":
			// { profile, fingerprint: Fingerprint (profiles.json's) | null }
			let profileID = try string("profile")
			var fingerprint: Fingerprint?

			if let fields = command["fingerprint"] as? [String: Any] {
				let data = try JSONSerialization.data(withJSONObject: fields)
				fingerprint = try JSONDecoder().decode(Fingerprint.self, from: data)
			}

			model.updateProfile(profileID) { $0.fingerprint = fingerprint }

		case "showPinScreen":
			// Settings' Set up, Change, or Remove PIN.
			switch try string("mode") {
			case "setup": controller.lock.show(.setup)
			case "change": controller.lock.show(.change)
			case "remove": controller.lock.show(.remove)
			default: throw AutomationError("unknown PIN screen mode")
			}

		case "pinKeys":
			// { keys: "1234" }: the numpad's digits; "<" is Delete, ">" is
			// Continue, "^" is the back arrow.
			for key in try string("keys") {
				switch key {
				case "<": controller.lock.backspace()
				case ">": controller.lock.continueWithPin()
				case "^": controller.lock.dismiss()
				default: controller.lock.digit(key)
				}
			}

		case "setSleepSettings":
			// { tabSleepAfterMinutes, profileUnloadAfterMinutes, cacheBudgetMB },
			// each a number or null (off).
			model.settings.tabSleepAfterMinutes = command["tabSleepAfterMinutes"] as? Int
			model.settings.profileUnloadAfterMinutes = command["profileUnloadAfterMinutes"] as? Int
			model.settings.cacheBudgetMB = command["cacheBudgetMB"] as? Int

		case "setKeepLoaded":
			let keepLoaded = command["keepLoaded"] as? Bool ?? true
			model.updateProfile(try string("profile")) { $0.keepLoaded = keepLoaded }

		case "readCacheSize":
			// { profile }: the snapshot's profiles[].cacheSize has it once read.
			let profileID = try string("profile")

			guard let context = controller.context(for: profileID) else {
				throw AutomationError("profile \(profileID) is not open")
			}

			cacheSizes[profileID] = nil

			Task { [weak self] in
				self?.cacheSizes[profileID] = await context.cacheSize()
			}

		case "readCookies":
			// { profile }: read in the background; the snapshot's
			// profiles[].cookies has them once read.
			let profileID = try string("profile")

			guard let context = controller.context(for: profileID) else {
				throw AutomationError("profile \(profileID) is not open")
			}

			cookies[profileID] = nil

			Task { [weak self] in
				let all = await context.cookies()
				self?.cookies[profileID] = all.map { ["name": $0.name, "domain": $0.domain, "path": $0.path] }
			}

		case "installExtension":
			controller.installExtension(try string("id"), inProfile: try string("profile"))

		case "runExtensionAction":
			controller.runExtensionAction(try string("id"))

		case "setExtensionEnabled":
			controller.setExtension(try string("id"), enabled: command["enabled"] as? Bool ?? true, inProfile: try string("profile"))

		case "setExtensionPinned":
			controller.setExtension(try string("id"), pinned: command["pinned"] as? Bool ?? true, inProfile: try string("profile"))

		case "uninstallExtension":
			controller.uninstallExtension(try string("id"), inProfile: try string("profile"))

		case "updateExtensions":
			controller.updateExtensions(inProfile: try string("profile"))

		case "closeExtensionPopup":
			controller.extensionPopover.close()

		case "render":
			// Draws the window's app UI (not the pages, which the GPU process
			// composites) into a PNG, for checks while the screen is locked.
			try render(to: URL(fileURLWithPath: try string("path")))

		case "save":
			model.saveNow()

		case "quit":
			model.saveNow()
			NSApp.terminate(nil)

		default:
			throw AutomationError("unknown command \(name)")
		}

		return nil
	}

	private func clickSheetButton(_ title: String, texts: [String]) throws {
		guard let alert = controller.dialogs.presented?.alert else {
			throw AutomationError("no sheet")
		}

		guard let button = alert.buttons.first(where: { $0.title == title }) else {
			throw AutomationError("no button \(title); buttons: \(alert.buttons.map(\.title))")
		}

		for (field, text) in zip(Self.textFields(in: alert.accessoryView), texts) {
			field.stringValue = text
		}

		button.performClick(nil)
	}

	private func answerFileChooser(paths: [String]?, cancel: Bool) throws {
		let urls = cancel ? nil : (paths ?? []).map { URL(fileURLWithPath: $0) }

		if !controller.dialogs.answerPanel(with: urls) {
			throw AutomationError("no file chooser")
		}
	}

	// What a view shows, for telling windows apart: its tooltips, labels,
	// and button titles.
	private static func contents(of view: NSView?) -> [String] {
		guard let view else {
			return []
		}

		var parts: [String] = []

		if let tip = view.toolTip, !tip.isEmpty {
			parts.append("tooltip: \(tip)")
		}

		if let label = view.accessibilityLabel(), !label.isEmpty {
			parts.append("label: \(label)")
		}

		if let button = view as? NSButton, !button.title.isEmpty {
			parts.append("button: \(button.title)")
		}

		return parts + view.subviews.flatMap { contents(of: $0) }
	}

	private static func buttons(in view: NSView?) -> [NSButton] {
		guard let view else {
			return []
		}

		return ((view as? NSButton).map { [$0] } ?? []) + view.subviews.flatMap { buttons(in: $0) }
	}

	private static func textFields(in view: NSView?) -> [NSTextField] {
		guard let view else {
			return []
		}

		if let field = view as? NSTextField, field.isEditable {
			return [field]
		}

		return view.subviews.flatMap { textFields(in: $0) }
	}

	// Every visible window of the process, by what it is to the app: its
	// window, the sheet on it, an app-modal panel, or other (Chrome's own).
	private func nativeWindows() -> [[String: Any]] {
		let main = controller.browserWindow.window

		return NSApp.windows.filter(\.isVisible).map { window in
			let role = if window === main {
				"main"
			} else if window === main.attachedSheet {
				"sheet"
			} else if window === NSApp.modalWindow {
				"modal"
			} else if controller.contextMenuOpen, String(describing: type(of: window)).contains("Menu") {
				"menu"
			} else if window.identifier?.rawValue == "pane.devtools" {
				"devtools"
			} else if let popover = controller.extensionPopover.window, window === popover {
				"popover"
			} else if ["_NSFullScreenTransition", "NSToolbarFullScreenWindow"].contains(where: String(describing: type(of: window)).hasPrefix) {
				// AppKit's own: the full screen animation, and the title bar
				// it reveals at the top of a full screen window.
				"system"
			} else {
				"other"
			}

			return [
				"role": role,
				"class": String(describing: type(of: window)),
				"title": window.title,
				"frame": NSStringFromRect(window.frame),
				"shows": Self.contents(of: window.contentView),
			]
		}
	}

	private func sheetSnapshot() -> Any {
		guard let presented = controller.dialogs.presented else {
			return NSNull()
		}

		if let alert = presented.alert {
			return [
				"kind": presented.pending.request.kind,
				"tabID": presented.pending.tabID ?? NSNull(),
				"title": alert.messageText,
				"message": alert.informativeText,
				"buttons": alert.buttons.map(\.title),
				"fields": Self.textFields(in: alert.accessoryView).map(\.stringValue),
			] as [String: Any]
		}

		return [
			"kind": presented.pending.request.kind,
			"panel": presented.panel is NSOpenPanel ? "open" : "save",
			"message": presented.panel?.message ?? "",
		] as [String: Any]
	}

	private static let keyCodes: [String: UInt16] = [
		"a": 0, "f": 3, "i": 34, "p": 35, "r": 15, "t": 17, "w": 13, "l": 37,
		"1": 18, "2": 19, "3": 20, "0": 29, "=": 24, "-": 27,
		"[": 33, "]": 30, ",": 43, "\t": 48, "\u{1b}": 53,
	]

	private func modifierFlags(_ names: [String]) -> NSEvent.ModifierFlags {
		names.reduce(into: NSEvent.ModifierFlags()) { flags, name in
			switch name {
			case "command": flags.insert(.command)
			case "shift": flags.insert(.shift)
			case "control": flags.insert(.control)
			case "option": flags.insert(.option)
			default: break
			}
		}
	}

	// Whether an image draws anything: an icon can be there and blank.
	private static func hasVisiblePixels(_ image: NSImage) -> Bool {
		guard
			let bitmap = NSBitmapImageRep(
				bitmapDataPlanes: nil, pixelsWide: 16, pixelsHigh: 16, bitsPerSample: 8,
				samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB,
				bytesPerRow: 0, bitsPerPixel: 0
			),
			let context = NSGraphicsContext(bitmapImageRep: bitmap)
		else {
			return false
		}

		NSGraphicsContext.saveGraphicsState()
		NSGraphicsContext.current = context
		image.draw(in: NSRect(x: 0, y: 0, width: 16, height: 16))
		NSGraphicsContext.restoreGraphicsState()

		return (0..<16).contains { x in
			(0..<16).contains { y in (bitmap.colorAt(x: x, y: y)?.alphaComponent ?? 0) > 0.3 }
		}
	}

	private func postKey(_ key: String, modifiers: [String]) throws {
		guard let keyCode = Self.keyCodes[key] else {
			throw AutomationError("no key code for \(key)")
		}

		let flags = modifierFlags(modifiers)
		let window = controller.browserWindow.window
		// As typed: Shift makes a letter uppercase.
		let characters = flags.contains(.shift) ? key.uppercased() : key

		if !flags.isEmpty {
			post(flagsChanged: flags)
		}

		for type in [NSEvent.EventType.keyDown, .keyUp] {
			let event = NSEvent.keyEvent(
				with: type,
				location: .zero,
				modifierFlags: flags,
				timestamp: ProcessInfo.processInfo.systemUptime,
				windowNumber: window.windowNumber,
				context: nil,
				characters: characters,
				charactersIgnoringModifiers: characters,
				isARepeat: false,
				keyCode: keyCode
			)

			if let event {
				NSApp.postEvent(event, atStart: false)
			}
		}
	}

	private func post(flagsChanged flags: NSEvent.ModifierFlags) {
		let event = NSEvent.keyEvent(
			with: .flagsChanged,
			location: .zero,
			modifierFlags: flags,
			timestamp: ProcessInfo.processInfo.systemUptime,
			windowNumber: controller.browserWindow.window.windowNumber,
			context: nil,
			characters: "",
			charactersIgnoringModifiers: "",
			isARepeat: false,
			keyCode: flags.contains(.control) ? 59 : 0
		)

		if let event {
			NSApp.postEvent(event, atStart: false)
		}
	}

	// What has keyboard focus: a text field being edited, by its placeholder
	// (SwiftUI keeps accessibility identifiers off the AppKit field), else
	// the focused view's identifier or class.
	private var focus: String {
		let responder = controller.browserWindow.window.firstResponder

		if let editor = responder as? NSTextView, let field = editor.delegate as? NSTextField {
			// SwiftUI gives a prompt as an attributed placeholder.
			return "field: \(field.placeholderString ?? field.placeholderAttributedString?.string ?? "")"
		}

		if let view = responder as? NSView, !view.accessibilityIdentifier().isEmpty {
			return view.accessibilityIdentifier()
		}

		return responder.map { String(describing: type(of: $0)) } ?? "none"
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

	// MARK: State

	private func snapshot() -> [String: Any] {
		let model = controller.model
		let state = controller.state
		let window = controller.browserWindow.window

		return [
			"profiles": model.profiles.map { profile in
				[
					"id": profile.id,
					"name": profile.name,
					"color": profile.color.rawValue,
					"expanded": model.expandedProfileIDs.contains(profile.id),
					"proxyStatus": model.proxyStatus[profile.id]?.rawValue ?? NSNull(),
					"keepLoaded": profile.keepLoaded,
					"cacheSize": cacheSizes[profile.id].map { $0 as Any } ?? NSNull(),
					"cookies": cookies[profile.id] ?? NSNull(),
					// What the engine applied to the open profile.
					"network": controller.context(for: profile.id).map { context in
						[
							"proxyRules": context.proxyRules ?? NSNull(),
							"webRTCIPHandlingPolicy": context.webRTCIPHandlingPolicy,
						] as [String: Any]
					} ?? NSNull(),
					"tabs": profile.tabs.map { tab in
						[
							"id": tab.id,
							"url": tab.url,
							"title": tab.title,
							"hasFavicon": !tab.favicon.isEmpty,
							"loaded": model.loadedTabIDs.contains(tab.id),
							"loading": model.loadingProgress[tab.id] != nil,
							"canGoBack": model.navigation[tab.id]?.canGoBack ?? false,
							"canGoForward": model.navigation[tab.id]?.canGoForward ?? false,
						] as [String: Any]
					},
				] as [String: Any]
			},
			"security": [
				"pinLength": controller.lock.state.pin?.length ?? NSNull(),
				"failedAttempts": controller.lock.state.failedAttempts,
				"locked": controller.lock.isLocked,
				"screen": controller.lock.entry.map { entry in
					[
						"mode": "\(entry.mode)",
						"step": "\(entry.step)",
						"title": entry.title ?? NSNull(),
						"dots": entry.dots,
						"entered": entry.entered.count,
						"busy": entry.busy,
						"error": entry.error ?? NSNull(),
					] as [String: Any]
				} ?? NSNull(),
			] as [String: Any],
			"activeTabID": model.activeTabID ?? NSNull(),
			"activeProfileID": model.activeProfileID ?? NSNull(),
			"page": model.page == .browser ? "browser" : "settings",
			"theme": model.settings.theme.rawValue,
			"appearance": NSApp.effectiveAppearance.bestMatch(from: [.aqua, .darkAqua])?.rawValue ?? "",
			"find": [
				"visible": state.find.visible,
				"text": state.find.text,
				"matches": state.find.matches,
				"activeMatch": state.find.activeMatch,
			] as [String: Any],
			"switcher": [
				"visible": state.switcher.visible,
				"selectedIndex": state.switcher.selectedIndex,
				"groups": state.switcher.groups.map { group in
					["profileID": group.id, "name": group.name, "tabs": group.tabs.map(\.id)] as [String: Any]
				},
			] as [String: Any],
			"downloads": Array(downloads.values),
			"address": controller.state.address,
			"window": [
				"title": window.title,
				"key": window.isKeyWindow,
				"visible": window.isVisible,
				"fullScreen": window.styleMask.contains(.fullScreen),
				"frame": NSStringFromRect(window.frame),
			] as [String: Any],
			"closedTabs": model.history.closed.count,
			"pendingProfileDeletions": controller.pendingDeletions.count,
			"sheet": sheetSnapshot(),
			"pendingRequests": controller.dialogs.pendingCount,
			"contextMenu": controller.lastContextMenu.map { menu in
				menu.items.filter { !$0.isSeparatorItem }.map { ["title": $0.title, "enabled": $0.isEnabled] as [String: Any] }
			} ?? NSNull(),
			"modal": (NSApp.modalWindow).map { modal in
				["title": modal.title, "buttons": Self.buttons(in: modal.contentView).map(\.title)] as [String: Any]
			} ?? NSNull(),
			"nativeWindows": nativeWindows(),
			"extensions": model.extensions.mapValues { list in
				list.map { item in
					[
						"id": item.id,
						"name": item.name,
						"version": item.version,
						"enabled": item.enabled,
						"pinned": item.pinned,
						"hasAction": item.hasAction,
						"hasPopup": item.hasPopup,
						"actionTitle": item.actionTitle,
						"badgeText": item.badgeText,
						"hasIcon": item.icon != nil,
						"iconVisible": item.icon.map(Self.hasVisiblePixels) ?? false,
					] as [String: Any]
				}
			},
			"extensionPopup": controller.extensionPopover.popup.map { popup in
				[
					"extension": popup.extensionIdentifier,
					"open": popup.isOpen,
					"width": popup.preferredSize.width,
					"height": popup.preferredSize.height,
				] as [String: Any]
			} ?? NSNull(),
			"installedExtension": controller.state.installedExtension ?? NSNull(),
			"pageFullscreen": controller.state.pageFullscreen,
			"exclusiveAccessHint": controller.state.exclusiveAccessHint ?? NSNull(),
			"crashedTabs": Array(model.crashedTabIDs),
			"pictureInPictureTabs": Array(model.pictureInPictureTabIDs),
			"devToolsTabs": Array(model.devToolsTabIDs),
			"printRequests": controller.state.printRequests,
			"focus": focus,
		]
	}

	private static func describe(_ download: Download) -> [String: Any] {
		let state = switch download.state {
		case .inProgress: "inProgress"
		case .complete: "complete"
		case .cancelled: "cancelled"
		case .interrupted: "interrupted"
		@unknown default: "unknown"
		}

		return [
			"id": download.identifier,
			"url": download.url.absoluteString,
			"path": download.fileURL?.path ?? NSNull(),
			"receivedBytes": download.receivedBytes,
			"totalBytes": download.totalBytes,
			"state": state,
		]
	}
}

private struct AutomationError: Error, CustomStringConvertible {
	let description: String

	init(_ description: String) {
		self.description = description
	}
}
