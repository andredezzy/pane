import AppKit
import PaneCore
import PaneKit

// A test-only command channel, on when PANE_AUTOMATION_DIR is set in a
// scenario's run, on the mock keychain (TestHooks), never otherwise: the
// scenario runner drops JSON commands into `<dir>/in/<n>.json` and reads the
// result, with a snapshot of the app's state, from `<dir>/out/<n>.json`.
// Its commands call the controller's intents and actions, the ones the UI
// calls, so scenarios exercise the app's paths, not test-only ones
// (Automation+Commands); keys and clicks are real events
// (Automation+Events); the snapshot reads what shows (Automation+Snapshot).
// It is the controller's probe, for what the app itself never reads.
@MainActor
final class Automation {
	private let inbox: URL
	private let outbox: URL
	let controller: BrowserController
	// Where the model saves: `save` and `quit` wait for the write.
	let store: ModelStore
	private var timer: Timer?
	// Where pointerWindow's last phase was, for the next move's distance.
	var lastPointer: NSPoint?
	// Each profile's cookies (names, domains, paths, and attributes; never
	// values), as last read with readCookies.
	var cookies: [String: [[String: Any]]] = [:]
	// Each profile's HTTP cache size in bytes, as last read with
	// readCacheSize.
	var cacheSizes: [String: Int64] = [:]

	// What the probe reported. The context menu is held weakly: it is alive
	// while it shows, and its items keep their page.
	private(set) var downloads: [String: [String: Any]] = [:]
	private(set) weak var contextMenu: NSMenu?
	private(set) var contextMenuOpen = false
	// When the last one closed: AppKit fades a menu's window out after it
	// closes, and on a busy Mac it stayed visible for most of a second.
	private(set) var contextMenuClosed = Date.distantPast
	// Each menu a page has shown, counted, so a scenario can wait for the
	// next one rather than read the last.
	private(set) var contextMenusShown = 0
	private(set) var printRequests = 0
	private(set) var pendingDeletions: Set<String> = []

	init(directory: URL, controller: BrowserController, store: ModelStore) {
		inbox = directory.appendingPathComponent("in", isDirectory: true)
		outbox = directory.appendingPathComponent("out", isDirectory: true)
		self.controller = controller
		self.store = store

		try? FileManager.default.createDirectory(at: inbox, withIntermediateDirectories: true)
		try? FileManager.default.createDirectory(at: outbox, withIntermediateDirectories: true)

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
				guard let data, let fields = try JSONSerialization.jsonObject(with: data) as? [String: Any] else {
					throw AutomationError("unreadable command")
				}

				reply = ["ok": true, "result": try run(AutomationCommand(fields)) ?? NSNull()]
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
}

extension Automation: AutomationProbe {
	func pageWillPrint() {
		printRequests += 1
	}

	func contextMenu(_ menu: NSMenu, isOpen: Bool) {
		if menu !== contextMenu {
			contextMenusShown += 1
		}

		contextMenu = menu
		contextMenuOpen = isOpen

		if !isOpen {
			contextMenuClosed = Date()
		}
	}

	func downloadDidUpdate(_ download: Download) {
		downloads[download.identifier] = Self.describe(download)
	}

	func profileDeletion(_ profileID: String, isPending: Bool) {
		if isPending {
			pendingDeletions.insert(profileID)
		} else {
			pendingDeletions.remove(profileID)
		}
	}
}

// One command: its name and fields, read with the error a scenario needs.
struct AutomationCommand {
	let name: String
	private let fields: [String: Any]

	init(_ fields: [String: Any]) {
		name = fields["command"] as? String ?? ""
		self.fields = fields
	}

	subscript(key: String) -> Any? {
		fields[key]
	}

	func string(_ key: String) throws -> String {
		try required(key)
	}

	func int(_ key: String) throws -> Int {
		try required(key)
	}

	// A point of the window or the page, top-left origin: { x, y }.
	func point() throws -> NSPoint {
		guard let x = fields["x"] as? Double, let y = fields["y"] as? Double else {
			throw AutomationError("\(name) needs x and y")
		}

		return NSPoint(x: x, y: y)
	}

	private func required<Value>(_ key: String) throws -> Value {
		guard let value = fields[key] as? Value else {
			throw AutomationError("\(name) needs \(key)")
		}

		return value
	}
}

struct AutomationError: Error, CustomStringConvertible {
	let description: String

	init(_ description: String) {
		self.description = description
	}
}
