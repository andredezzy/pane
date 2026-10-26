import AppKit
import PaneCore
import UniformTypeIdentifiers

// Shows the engine's requests (PaneDialogs.h) as native sheets on the window,
// one at a time. A tab's requests show only while the tab is selected, as
// Chrome's tab-modal dialogs do; the rest wait their turn. File choosers
// belong to the window and show whichever tab is selected.
@MainActor
final class DialogPresenter {
	@MainActor
	enum Request {
		case javaScript(JavaScriptDialog)
		case permission(PermissionRequest)
		case authentication(AuthenticationChallenge)
		case externalProtocol(ExternalProtocolRequest)
		case unresponsive(UnresponsivePage)
		case fileChooser(FileChooser)

		var object: AnyObject {
			switch self {
			case let .javaScript(request): request
			case let .permission(request): request
			case let .authentication(request): request
			case let .externalProtocol(request): request
			case let .unresponsive(request): request
			case let .fileChooser(request): request
			}
		}

		var isAnswered: Bool {
			switch self {
			case let .javaScript(request): request.isAnswered
			case let .permission(request): request.isAnswered
			case let .authentication(request): request.isAnswered
			case let .externalProtocol(request): request.isAnswered
			case let .unresponsive(request): request.isAnswered
			case let .fileChooser(request): request.isAnswered
			}
		}

		var kind: String {
			switch self {
			case .javaScript: "javaScript"
			case .permission: "permission"
			case .authentication: "authentication"
			case .externalProtocol: "externalProtocol"
			case .unresponsive: "unresponsive"
			case .fileChooser: "fileChooser"
			}
		}
	}

	struct Pending {
		// nil for the window's own requests (file choosers).
		let tabID: String?
		let request: Request
	}

	// What is on screen: an alert, or a file panel.
	struct Presented {
		let pending: Pending
		let alert: NSAlert?
		let panel: NSSavePanel?
		let token: Token
	}

	// Identifies one showing of a sheet. A sheet the app takes down
	// unanswered is retracted, so its completion answers nothing.
	final class Token {
		var retracted = false
	}

	private let window: NSWindow
	private let activeTabID: () -> String?
	private var queue: [Pending] = []
	private(set) var presented: Presented?

	var pendingCount: Int {
		queue.count
	}

	init(window: NSWindow, activeTabID: @escaping () -> String?) {
		self.window = window
		self.activeTabID = activeTabID
	}

	func enqueue(_ request: Request, tabID: String?) {
		queue.append(Pending(tabID: tabID, request: request))
		presentNext()
	}

	// The engine took a request back (didDismissRequest).
	func withdraw(_ object: AnyObject) {
		if let presented, presented.pending.request.object === object {
			retract()
		}

		queue.removeAll { $0.request.object === object }
		presentNext()
	}

	// The selected tab changed: a sheet of another tab goes back in line.
	func selectionChanged() {
		if let presented, let tabID = presented.pending.tabID, tabID != activeTabID() {
			retract()
			queue.insert(presented.pending, at: 0)
		}

		presentNext()
	}

	// Answers the shown file panel with `urls` (nil: cancel) without the user
	// picking them: for scenarios on a locked screen. Open and save panels run
	// out of process on macOS 27 and take no programmatic choice, so the
	// panel is taken down and the chooser answered beside it.
	func answerPanel(with urls: [URL]?) -> Bool {
		guard let presented, case let .fileChooser(chooser) = presented.pending.request else {
			return false
		}

		retract()

		if let urls {
			chooser.choose(urls)
		} else {
			chooser.cancel()
		}

		DispatchQueue.main.async { [weak self] in
			self?.presentNext()
		}

		return true
	}

	// MARK: Presenting

	private func presentNext() {
		queue.removeAll { $0.request.isAnswered }

		guard
			presented == nil,
			window.attachedSheet == nil,
			let index = queue.firstIndex(where: { $0.tabID == nil || $0.tabID == activeTabID() })
		else {
			return
		}

		present(queue.remove(at: index))
	}

	private func present(_ pending: Pending) {
		if case let .fileChooser(chooser) = pending.request {
			presentPanel(for: chooser, pending: pending)
			return
		}

		let (alert, answer) = alert(for: pending.request)
		let token = Token()
		presented = Presented(pending: pending, alert: alert, panel: nil, token: token)
		alert.beginSheetModal(for: window) { [weak self] response in
			MainActor.assumeIsolated {
				if !token.retracted {
					answer(response)
				}

				self?.sheetEnded(token)
			}
		}
	}

	private func sheetEnded(_ token: Token) {
		if presented?.token === token {
			presented = nil
		}

		// The next sheet waits for this one to finish leaving the window.
		DispatchQueue.main.async { [weak self] in
			self?.presentNext()
		}
	}

	private func retract() {
		guard let presented else {
			return
		}

		presented.token.retracted = true
		self.presented = nil

		if let sheet = presented.alert?.window ?? presented.panel {
			window.endSheet(sheet, returnCode: .abort)
		}
	}

	// MARK: Alerts

	// An alert for the request, and what each of its buttons answers.
	private func alert(for request: Request) -> (NSAlert, (NSApplication.ModalResponse) -> Void) {
		let alert = NSAlert()
		let first = NSApplication.ModalResponse.alertFirstButtonReturn
		let second = NSApplication.ModalResponse.alertSecondButtonReturn

		switch request {
		case let .javaScript(dialog):
			return javaScriptAlert(alert, dialog)

		case let .permission(request):
			alert.messageText = "\(Self.host(request.originURL)) wants to"
			alert.informativeText = request.permissions.map(Self.describe).joined(separator: "\n")
			alert.addButton(withTitle: "Allow")
			alert.addButton(withTitle: "Allow This Time")
			alert.addButton(withTitle: "Don’t Allow")

			return (alert, { response in
				switch response {
				case first: request.allow()
				case second: request.allowThisTime()
				case .alertThirdButtonReturn: request.deny()
				default: request.dismiss()
				}
			})

		case let .authentication(challenge):
			let username = NSTextField(string: "")
			username.placeholderString = "Username"
			let password = NSSecureTextField(string: "")
			password.placeholderString = "Password"
			let fields = NSStackView(views: [username, password])
			fields.orientation = .vertical
			fields.spacing = 8
			fields.frame = NSRect(x: 0, y: 0, width: 260, height: 54)
			username.frame.size.width = 260
			password.frame.size.width = 260

			alert.messageText = "Sign in"
			alert.informativeText = challenge.isForProxy
				? "The proxy \(challenge.host) requires a username and password."
				: "\(challenge.host) requires a username and password."
			alert.accessoryView = fields
			alert.addButton(withTitle: "Sign In")
			alert.addButton(withTitle: "Cancel")
			alert.window.initialFirstResponder = username

			return (alert, { response in
				if response == first {
					challenge.provide(username: username.stringValue, password: password.stringValue)
				} else {
					challenge.cancel()
				}
			})

		case let .externalProtocol(request):
			alert.messageText = "Open “\(request.applicationName)”?"
			alert.informativeText = request.originURL.map { "\(Self.host($0)) wants to open this application." }
				?? "A website wants to open this application."
			alert.addButton(withTitle: "Open \(request.applicationName)")
			alert.addButton(withTitle: "Cancel")

			return (alert, { response in
				response == first ? request.open() : request.cancel()
			})

		case let .unresponsive(page):
			alert.messageText = "Page Unresponsive"
			alert.informativeText = "You can wait for it to become responsive, or exit the page."
			alert.addButton(withTitle: "Wait")
			alert.addButton(withTitle: "Exit Page")

			return (alert, { response in
				response == second ? page.terminate() : page.wait()
			})

		case .fileChooser:
			preconditionFailure("file choosers show a panel")
		}
	}

	private func javaScriptAlert(_ alert: NSAlert, _ dialog: JavaScriptDialog) -> (NSAlert, (NSApplication.ModalResponse) -> Void) {
		let origin = dialog.originURL.map { "\(Self.host($0)) says" } ?? "This page says"
		var field: NSTextField?

		switch dialog.type {
		case .alert:
			alert.messageText = origin
			alert.informativeText = dialog.message
			alert.addButton(withTitle: "OK")

		case .confirm:
			alert.messageText = origin
			alert.informativeText = dialog.message
			alert.addButton(withTitle: "OK")
			alert.addButton(withTitle: "Cancel")

		case .prompt:
			let input = NSTextField(string: dialog.defaultPromptText)
			input.frame = NSRect(x: 0, y: 0, width: 260, height: 24)
			field = input
			alert.messageText = origin
			alert.informativeText = dialog.message
			alert.accessoryView = input
			alert.addButton(withTitle: "OK")
			alert.addButton(withTitle: "Cancel")
			alert.window.initialFirstResponder = input

		case .beforeUnload:
			alert.messageText = dialog.isReload ? "Reload site?" : "Leave site?"
			alert.informativeText = "Changes you made may not be saved."
			alert.addButton(withTitle: dialog.isReload ? "Reload" : "Leave")
			alert.addButton(withTitle: "Cancel")

		case .formResubmission:
			alert.messageText = "Confirm Form Resubmission"
			alert.informativeText = "This page needs data you entered earlier. Sending it again repeats any action the page performed."
			alert.addButton(withTitle: "Continue")
			alert.addButton(withTitle: "Cancel")

		@unknown default:
			alert.messageText = origin
			alert.informativeText = dialog.message
			alert.addButton(withTitle: "OK")
		}

		return (alert, { response in
			if response == .alertFirstButtonReturn {
				dialog.accept(text: field?.stringValue)
			} else {
				dialog.cancel()
			}
		})
	}

	// MARK: File panels

	private func presentPanel(for chooser: FileChooser, pending: Pending) {
		let panel: NSSavePanel

		if chooser.mode == .save {
			panel = NSSavePanel()
			panel.nameFieldStringValue = chooser.defaultFileName
		} else {
			let open = NSOpenPanel()
			open.canChooseFiles = chooser.mode != .openFolder
			open.canChooseDirectories = chooser.mode == .openFolder
			open.allowsMultipleSelection = chooser.mode == .openMultiple
			panel = open
		}

		if !chooser.title.isEmpty {
			panel.message = chooser.title
		}

		let types = chooser.allowedFileExtensions.compactMap { UTType(filenameExtension: $0) }

		if !types.isEmpty {
			panel.allowedContentTypes = types
		}

		panel.directoryURL = chooser.directoryURL
		let token = Token()
		presented = Presented(pending: pending, alert: nil, panel: panel, token: token)

		panel.beginSheetModal(for: window) { [weak self] response in
			MainActor.assumeIsolated {
				if !token.retracted {
					if response == .OK {
						chooser.choose((panel as? NSOpenPanel)?.urls ?? panel.url.map { [$0] } ?? [])
					} else {
						chooser.cancel()
					}
				}

				self?.sheetEnded(token)
			}
		}
	}

	// MARK: Text

	static func host(_ url: URL) -> String {
		url.host() ?? url.absoluteString
	}

	// Chrome's permission keys, as the prompt lists them.
	static func describe(_ permission: String) -> String {
		switch permission {
		case "geolocation", "geolocation_approximate": "• Know your location"
		case "notifications": "• Show notifications"
		case "media_stream_camera": "• Use your camera"
		case "media_stream_mic": "• Use your microphone"
		case "camera_pan_tilt_zoom": "• Move your camera"
		case "clipboard": "• See text and images copied to the clipboard"
		case "midi_sysex": "• Control and reprogram your MIDI devices"
		case "storage_access", "top_level_storage_access": "• Use cookies and site data in this context"
		case "window_management": "• Manage windows on all your displays"
		case "local_fonts": "• Use your computer’s fonts"
		case "idle_detection": "• Know when you’re actively using this device"
		case "multiple_downloads": "• Download multiple files"
		case "protected_media_identifier": "• Play protected content"
		case "ar_session", "vr_session": "• Use your device’s sensors for virtual reality"
		case "hand_tracking": "• Track your hands"
		case "keyboard_lock": "• Use your keyboard"
		case "pointer_lock": "• Use your mouse pointer"
		default: "• \(permission.replacingOccurrences(of: "_", with: " "))"
		}
	}
}
