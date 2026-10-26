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

		case "releaseModifiers":
			post(flagsChanged: [])

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

	private static let keyCodes: [String: UInt16] = [
		"a": 0, "f": 3, "r": 15, "t": 17, "w": 13, "l": 37,
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
			return "field: \(field.placeholderString ?? "")"
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
			"window": [
				"title": window.title,
				"key": window.isKeyWindow,
				"visible": window.isVisible,
				"fullScreen": window.styleMask.contains(.fullScreen),
				"frame": NSStringFromRect(window.frame),
			] as [String: Any],
			"closedTabs": model.history.closed.count,
			"pendingProfileDeletions": controller.pendingDeletions.count,
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
