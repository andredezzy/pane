import Foundation
import os

// Profiles deleted whose engine data may still be on disk, kept in
// deletions.json from the delete until the engine confirms it. A quit, a
// crash, or a context that would not open leaves an id there, and the next
// launch removes that profile's folders before the engine starts, when
// nothing has them open.
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

	// Removes each pending profile's engine folder and caches, and returns
	// what it could not remove, which stays pending.
	public func purge(dataDirectory: URL, cacheDirectory: URL?) -> [String] {
		var failed: [String] = []

		for profileID in pending {
			// An id names one folder of Engine, never a path out of it.
			guard !profileID.isEmpty, !profileID.contains("/"), profileID != ".", profileID != ".." else {
				remove(profileID)
				continue
			}

			let folders = [dataDirectory, cacheDirectory].compactMap(\.self).map {
				$0.appending(path: "Engine/\(profileID)", directoryHint: .isDirectory)
			}

			do {
				for folder in folders where FileManager.default.fileExists(atPath: folder.path) {
					try FileManager.default.removeItem(at: folder)
				}

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
