import CoreFoundation
import Foundation
import os

// A system alert for the launch, before the engine runs: nothing may touch
// NSApplication then (main.swift), so it is a CFUserNotification, which the
// system draws for the process.
enum LaunchAlert {
	enum Response {
		case defaultButton
		case alternateButton
		// `dismissWhen` held first.
		case dismissed
	}

	// Waits for a button, or for `dismissWhen` to hold (asked every second),
	// which takes the alert down by itself.
	static func show(
		title: String,
		message: String,
		defaultButton: String,
		alternateButton: String? = nil,
		dismissWhen: (() -> Bool)? = nil
	) -> Response {
		guard let notification = create(title: title, message: message, flags: 0, buttons: [
			kCFUserNotificationDefaultButtonTitleKey: defaultButton,
			kCFUserNotificationAlternateButtonTitleKey: alternateButton,
		]) else {
			return .dismissed
		}

		var flags: CFOptionFlags = 0

		while CFUserNotificationReceiveResponse(notification, 1, &flags) != 0 {
			if dismissWhen?() == true {
				CFUserNotificationCancel(notification)
				return .dismissed
			}
		}

		return flags & 0x3 == CFOptionFlags(kCFUserNotificationAlternateResponse) ? .alternateButton : .defaultButton
	}

	// A note without buttons, up until the work it names is done.
	static func note(title: String, message: String) -> Note? {
		create(title: title, message: message, flags: CFOptionFlags(kCFUserNotificationNoDefaultButtonFlag), buttons: [:])
			.map(Note.init)
	}

	final class Note {
		private let notification: CFUserNotification

		fileprivate init(_ notification: CFUserNotification) {
			self.notification = notification
		}

		func close() {
			CFUserNotificationCancel(notification)
		}
	}

	private static func create(title: String, message: String, flags: CFOptionFlags, buttons: [CFString: String?]) -> CFUserNotification? {
		var contents: [CFString: Any] = [
			kCFUserNotificationAlertHeaderKey: title,
			kCFUserNotificationAlertMessageKey: message,
		]

		for case let (key, value?) in buttons {
			contents[key] = value
		}

		if let icon = Bundle.main.url(forResource: "AppIcon", withExtension: "icns") {
			contents[kCFUserNotificationIconURLKey] = icon
		}

		var error: Int32 = 0
		let notification = CFUserNotificationCreate(nil, 0, flags | CFOptionFlags(kCFUserNotificationNoteAlertLevel), &error, contents as CFDictionary)

		if notification == nil {
			Log.app.error("Could not show the launch alert \"\(title)\": \(error)")
		}

		return notification
	}
}
