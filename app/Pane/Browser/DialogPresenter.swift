import AppKit
import PaneCore
import SwiftUI
import UniformTypeIdentifiers

// Shows the engine's requests (PaneDialogs.h) one at a time. A page's
// dialog (an alert, a permission, a sign-in, an external app, an
// unresponsive page) is drawn in the window (DialogView), as the NSAlert
// sheet it replaced looked; a sheet held the main thread, Chromium's UI
// thread, some 280 ms as it attached. A tab's requests show only while the
// tab is selected, as Chrome's tab-modal dialogs do; the rest wait their
// turn. File choosers stay the system's open and save panels, and belong to
// the window: they show whichever tab is selected.
@MainActor
@Observable
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

	// What is on screen: a dialog in the window, or a file panel.
	struct Presented {
		let pending: Pending
		let dialog: PageDialog?
		let panel: NSSavePanel?
		let token: Token
	}

	// Identifies one showing. A dialog or panel the app takes down unanswered
	// is retracted, so nothing it does after answers.
	final class Token {
		var retracted = false
	}

	private(set) var presented: Presented?

	@ObservationIgnored private let window: NSWindow
	@ObservationIgnored private let activeTabID: () -> String?
	// Behind the PIN lock requests wait: a dialog would show over the PIN
	// screen, with the page's text.
	@ObservationIgnored private let isHeld: () -> Bool
	@ObservationIgnored private var queue: [Pending] = []
	// The shown dialog's fields, by index: their text is what it answers.
	@ObservationIgnored private var fields: [Int: NSTextField] = [:]
	// What had the keyboard before a dialog's field took it, for after.
	@ObservationIgnored private weak var focusBefore: NSResponder?

	var pendingCount: Int {
		queue.count
	}

	// The dialog on screen, if a dialog is.
	var dialog: PageDialog? {
		presented?.dialog
	}

	init(window: NSWindow, activeTabID: @escaping () -> String?, isHeld: @escaping () -> Bool) {
		self.window = window
		self.activeTabID = activeTabID
		self.isHeld = isHeld
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

	// What held the requests let go.
	func resume() {
		presentNext()
	}

	// The selected tab changed: a dialog of another tab goes back in line.
	func selectionChanged() {
		if let presented, let tabID = presented.pending.tabID, tabID != activeTabID() {
			retract()
			queue.insert(presented.pending, at: 0)
		}

		presentNext()
	}

	// MARK: Answering the dialog

	// The shown dialog's fields, as they read now.
	var fieldTexts: [String] {
		(dialog?.fields ?? []).indices.map { fields[$0]?.stringValue ?? dialog?.fields[$0].text ?? "" }
	}

	// A field of the shown dialog, as it is drawn.
	func register(_ field: NSTextField, at index: Int) {
		fields[index] = field
	}

	// Types into the shown dialog's fields, in order: for scenarios.
	func fill(_ texts: [String]) {
		for (index, text) in texts.enumerated() {
			fields[index]?.stringValue = text
		}
	}

	// One of its buttons, by its index in `dialog.buttons`.
	func press(_ index: Int) {
		answer(.button(index))
	}

	// Return: its default button, the first.
	func confirm() {
		answer(.button(0))
	}

	// Esc: what Chrome does when its dialog is dismissed.
	func dismiss() {
		answer(.dismissed)
	}

	private func answer(_ answer: PageDialog.Answer) {
		guard let presented, let dialog = presented.dialog else {
			return
		}

		guard !presented.token.retracted else {
			return
		}

		// Once: a second press in the same frame answers nothing.
		presented.token.retracted = true
		dialog.answer(answer, fieldTexts)
		end(presented)
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
			!isHeld(),
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

		let dialog = Self.dialog(for: pending.request)
		fields = [:]

		if !dialog.fields.isEmpty {
			focusBefore = window.firstResponder
		}

		withAnimation(Motion.panel) {
			presented = Presented(pending: pending, dialog: dialog, panel: nil, token: Token())
		}
	}

	// The dialog goes; the next request waits for the main loop's next turn,
	// so one never replaces another within an update.
	private func end(_ shown: Presented) {
		withAnimation(Motion.panel) {
			if presented?.token === shown.token {
				presented = nil
			}
		}

		restoreFocus()

		DispatchQueue.main.async { [weak self] in
			self?.presentNext()
		}
	}

	private func retract() {
		guard let presented else {
			return
		}

		presented.token.retracted = true

		if let panel = presented.panel {
			self.presented = nil
			window.endSheet(panel, returnCode: .abort)
		} else {
			withAnimation(Motion.panel) {
				self.presented = nil
			}

			restoreFocus()
		}
	}

	// A dialog's field took the keyboard from the page: it goes back.
	private func restoreFocus() {
		guard let focusBefore else {
			return
		}

		self.focusBefore = nil

		if let view = focusBefore as? NSView, view.window === window {
			window.makeFirstResponder(view)
		}
	}

	// MARK: Dialogs

	// The request as its dialog: its text, fields, and buttons (the default
	// first, as an NSAlert orders them), and what each answer does.
	private static func dialog(for request: Request) -> PageDialog {
		switch request {
		case let .javaScript(dialog):
			return javaScriptDialog(dialog)

		case let .permission(request):
			return PageDialog(
				title: "\(host(request.originURL)) wants to",
				message: request.permissions.map(describe),
				buttons: ["Allow", "Allow This Time", "Don\u{2019}t Allow"]
			) { answer, _ in
				switch answer {
				case .button(0): request.allow()
				case .button(1): request.allowThisTime()
				case .button: request.deny()
				// Closed without a choice: the site may ask again.
				case .dismissed: request.dismiss()
				}
			}

		case let .authentication(challenge):
			return PageDialog(
				title: "Sign in",
				message: [challenge.isForProxy
					? "The proxy \(challenge.host) requires a username and password."
					: "\(challenge.host) requires a username and password."],
				fields: [.init(placeholder: "Username"), .init(placeholder: "Password", isSecure: true)],
				buttons: ["Sign In", "Cancel"]
			) { answer, texts in
				if answer == .button(0) {
					challenge.provide(username: texts[0], password: texts[1])
				} else {
					challenge.cancel()
				}
			}

		case let .externalProtocol(request):
			return PageDialog(
				title: "Open \u{201C}\(request.applicationName)\u{201D}?",
				message: [request.originURL.map { "\(host($0)) wants to open this application." } ?? "A website wants to open this application."],
				buttons: ["Open \(request.applicationName)", "Cancel"]
			) { answer, _ in
				answer == .button(0) ? request.open() : request.cancel()
			}

		case let .unresponsive(page):
			// Dismissed, it waits, as Chrome's dialog closed does.
			return PageDialog(
				title: "Page Unresponsive",
				message: ["You can wait for it to become responsive, or exit the page."],
				buttons: ["Wait", "Exit Page"]
			) { answer, _ in
				answer == .button(1) ? page.terminate() : page.wait()
			}

		case .fileChooser:
			preconditionFailure("file choosers show a panel")
		}
	}

	private static func javaScriptDialog(_ dialog: JavaScriptDialog) -> PageDialog {
		let origin = dialog.originURL.map { "\(host($0)) says" } ?? "This page says"
		let accept: @MainActor (PageDialog.Answer, [String]) -> Void = { answer, texts in
			if answer == .button(0) {
				dialog.accept(text: texts.first)
			} else {
				dialog.cancel()
			}
		}

		switch dialog.type {
		case .alert:
			return PageDialog(title: origin, message: [dialog.message], buttons: ["OK"], answer: accept)

		case .confirm:
			return PageDialog(title: origin, message: [dialog.message], buttons: ["OK", "Cancel"], answer: accept)

		case .prompt:
			return PageDialog(
				title: origin,
				message: [dialog.message],
				fields: [.init(placeholder: "", text: dialog.defaultPromptText)],
				buttons: ["OK", "Cancel"],
				answer: accept
			)

		case .beforeUnload:
			return PageDialog(
				title: dialog.isReload ? "Reload site?" : "Leave site?",
				message: ["Changes you made may not be saved."],
				buttons: [dialog.isReload ? "Reload" : "Leave", "Cancel"],
				answer: accept
			)

		case .formResubmission:
			return PageDialog(
				title: "Confirm Form Resubmission",
				message: ["This page needs data you entered earlier. Sending it again repeats any action the page performed."],
				buttons: ["Continue", "Cancel"],
				answer: accept
			)

		@unknown default:
			return PageDialog(title: origin, message: [dialog.message], buttons: ["OK"], answer: accept)
		}
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
		let shown = Presented(pending: pending, dialog: nil, panel: panel, token: Token())
		presented = shown

		panel.beginSheetModal(for: window) { [weak self] response in
			MainActor.assumeIsolated {
				if !shown.token.retracted {
					if response == .OK {
						chooser.choose((panel as? NSOpenPanel)?.urls ?? panel.url.map { [$0] } ?? [])
					} else {
						chooser.cancel()
					}
				}

				self?.panelEnded(shown.token)
			}
		}
	}

	private func panelEnded(_ token: Token) {
		if presented?.token === token {
			presented = nil
		}

		// The next request waits for the panel to finish leaving the window.
		DispatchQueue.main.async { [weak self] in
			self?.presentNext()
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

// A page's dialog as the window draws it: the title, the message's lines,
// the fields, and the buttons, the default one first.
struct PageDialog: Identifiable {
	enum Answer: Equatable {
		// One of the buttons, by index.
		case button(Int)
		// Esc: Chrome's dismissal, a cancel for most.
		case dismissed
	}

	struct Field {
		var placeholder: String
		var isSecure = false
		var text = ""
	}

	let id = UUID()
	let title: String
	let message: [String]
	var fields: [Field] = []
	let buttons: [String]
	// Answered once, with the fields' text.
	let answer: @MainActor (Answer, [String]) -> Void
}
