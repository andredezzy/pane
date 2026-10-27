import Foundation
import PaneKit

// The Electron app's extensions, still to reinstall from the Web Store in
// each migrated profile (migration.json's pending list). A profile installs
// them as it opens; their install prompts are answered for the user, who had
// them installed already. An install that fails stays pending for the next
// launch.
@MainActor
final class MigratedExtensions {
	private let file: PersistedFile<ElectronMigration.Record>
	private var record: ElectronMigration.Record?

	init(dataDirectory: URL) {
		file = PersistedFile(directory: dataDirectory, name: ElectronMigration.recordName)
		record = try? file.load()
	}

	func pending(in profileID: String) -> [String] {
		record?.pendingExtensions[profileID] ?? []
	}

	func isPending(_ extensionID: String, in profileID: String) -> Bool {
		pending(in: profileID).contains(extensionID)
	}

	func installed(_ extensionID: String, in profileID: String) {
		guard var record, isPending(extensionID, in: profileID) else {
			return
		}

		record.pendingExtensions[profileID]?.removeAll { $0 == extensionID }

		if record.pendingExtensions[profileID]?.isEmpty == true {
			record.pendingExtensions[profileID] = nil
		}

		self.record = record

		do {
			try file.save(record)
		} catch {
			NSLog("Pane: could not save migration.json: %@", String(describing: error))
		}
	}
}
