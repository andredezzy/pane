import Foundation
import PaneKit
import Security

// The Electron app's data comes over at launch, before anything reads the
// data directory or the engine starts (PaneKit ElectronMigration), and its
// cookie key with it (SafeStorage). The user hears of it through LaunchAlert.
enum LaunchMigration {
	static func run(from source: ElectronMigration.Source, into data: URL) {
		while true {
			let started = Date()
			let outcome: ElectronMigration.Outcome

			do {
				outcome = try migrate(from: source.directory, into: data)
			} catch {
				// Nothing committed (profiles.json moves last), so the next
				// launch tries again, unless the user makes profiles first.
				NSLog("Pane: the migration from the Electron app failed: %@", String(describing: error))
				return
			}

			NSLog("Pane: migration from the Electron app: %@ (%.1f s)", String(describing: outcome), Date().timeIntervalSince(started))

			switch outcome {
			case .sourceInUse:
				// Its files may be mid-write: Pane waits for it to quit, or
				// quits itself, having written nothing.
				let response = LaunchAlert.show(
					title: "Quit the old Pane to continue",
					message: "Pane brings over the old Pane\u{2019}s profiles, tabs, and sign-ins, and needs it closed to copy them. Quit it, and Pane goes on by itself.",
					defaultButton: "Cancel",
					dismissWhen: { !ElectronMigration.isRunning(source.directory) }
				)

				guard response == .dismissed else {
					exit(0)
				}

			case .migrated:
				adoptCookieKey(of: source, into: data, retrying: false)
				return

			case .notNeeded:
				adoptCookieKey(of: source, into: data, retrying: true)
				return

			case .noSource:
				return
			}
		}
	}

	// The copy runs on a thread of its own. A long one (gigabytes of site
	// data) shows a note after a second, so the launch never looks hung.
	private static func migrate(from source: URL, into data: URL) throws -> ElectronMigration.Outcome {
		final class Box: @unchecked Sendable {
			var result: Result<ElectronMigration.Outcome, Error>?
		}

		let box = Box()
		let done = DispatchSemaphore(value: 0)

		Thread {
			box.result = Result { try ElectronMigration.run(from: source, into: data) }
			done.signal()
		}.start()

		if done.wait(timeout: .now() + 1) == .timedOut {
			let note = LaunchAlert.note(
				title: "Bringing over your profiles",
				message: "Pane is copying the old Pane\u{2019}s profiles, tabs, and sign-ins. It opens once they\u{2019}re here."
			)

			done.wait()
			note?.close()
		}

		return try box.result!.get()
	}

	// Hands the Electron app's cookie key to the engine, so the copied
	// cookies decrypt. macOS asks the user first; a Deny asks again here,
	// since a Pane without the key signs the migrated sites out for good.
	// When the Keychain could not ask at all (a locked session), a later
	// launch tries again, while the engine has no key of its own: it would
	// have encrypted new cookies with it.
	private static func adoptCookieKey(of source: ElectronMigration.Source, into data: URL, retrying: Bool) {
		let file = PersistedFile<ElectronMigration.Record>(directory: data, name: ElectronMigration.recordName)

		guard
			let appName = source.appName,
			let service = source.keychainService,
			var record = try? file.load(),
			!retrying || (record.cookieKeyPending == true && !SafeStorage.hasEngineKey())
		else {
			return
		}

		var status = SafeStorage.adoptKey(service: service, account: appName)

		while status == errSecUserCanceled || status == errSecAuthFailed {
			let response = LaunchAlert.show(
				title: "Keep your sign-ins?",
				message: "Pane needs the old Pane\u{2019}s key in your keychain to keep the sites you were signed in to. Without it they\u{2019}re signed out; nothing else is lost.",
				defaultButton: "Try Again",
				alternateButton: "Continue Signed Out"
			)

			guard response == .defaultButton else {
				break
			}

			status = SafeStorage.adoptKey(service: service, account: appName)
		}

		NSLog("Pane: the Electron app's cookie key: %d", status)

		// Adopted, missing, or declined: done. Anything else could not ask.
		record.cookieKeyPending = ![errSecSuccess, errSecItemNotFound, errSecUserCanceled, errSecAuthFailed].contains(status)

		do {
			try file.save(record)
		} catch {
			NSLog("Pane: could not save migration.json: %@", String(describing: error))
		}
	}
}
