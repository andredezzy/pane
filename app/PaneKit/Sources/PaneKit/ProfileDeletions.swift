import Foundation
import os

// Profiles deleted whose engine data may still be on disk, kept in
// deletions.json from the delete until the engine confirms it. A quit, a
// crash, or a context that would not open leaves an id there, and the next
// launch removes that profile's data once the volume is attached, before
// anything opens a profile.
public struct ProfileDeletions: Sendable {
	private let file: PersistedFile<[String]>

	public init(dataDirectory: URL) {
		file = PersistedFile(directory: dataDirectory, name: "deletions")
	}

	public var pending: [String] {
		((try? file.load()) ?? nil) ?? []
	}

	public func add(_ profileID: String) {
		save(pending.filter { $0 != profileID } + [profileID])
	}

	public func remove(_ profileID: String) {
		save(pending.filter { $0 != profileID })
	}

	// Removes each pending profile's data (DataVolume.remove: its links, a
	// plain folder an earlier Pane left, and its folder in the volume, which
	// is attached by then), and returns what it could not remove, which
	// stays pending.
	public func purge(volume: DataVolume) -> [String] {
		var failed: [String] = []

		for profileID in pending {
			// An id names one folder of Engine, never a path out of it.
			guard !profileID.isEmpty, !profileID.contains("/"), profileID != ".", profileID != ".." else {
				remove(profileID)
				continue
			}

			do {
				try volume.remove(profile: profileID)
				remove(profileID)
			} catch {
				failed.append("\(profileID): \(error)")
			}
		}

		return failed
	}

	private func save(_ ids: [String]) {
		guard !ids.isEmpty else {
			try? FileManager.default.removeItem(at: file.url)
			return
		}

		do {
			try file.save(ids)
		} catch {
			Logger(subsystem: "com.pane.desktop", category: "model").error("Could not save deletions.json: \(String(describing: error), privacy: .private)")
		}
	}
}
