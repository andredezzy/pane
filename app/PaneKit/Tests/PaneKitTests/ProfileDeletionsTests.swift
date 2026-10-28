import Foundation
import PaneKit
import Testing

@Suite("ProfileDeletions")
struct ProfileDeletionsTests {
	private let fileManager = FileManager.default

	@Test("a delete the engine never confirmed is finished at the next launch")
	func purge() throws {
		let root = fileManager.temporaryDirectory.appending(path: "pane-deletions-\(UUID().uuidString)")
		let data = root.appending(path: "data")
		let caches = root.appending(path: "caches")
		defer { try? fileManager.removeItem(at: root) }

		for folder in ["data/Engine/gone/Network", "caches/Engine/gone/Cache", "data/Engine/kept", "outside"] {
			try fileManager.createDirectory(at: root.appending(path: folder), withIntermediateDirectories: true)
		}

		let deletions = ProfileDeletions(dataDirectory: data)
		deletions.add("gone")
		deletions.add("confirmed")
		deletions.remove("confirmed")
		// Never a path out of Engine.
		deletions.add("../../outside")

		#expect(ProfileDeletions(dataDirectory: data).pending == ["gone", "../../outside"])
		#expect(deletions.purge(dataDirectory: data, cacheDirectory: caches).isEmpty)

		#expect(!fileManager.fileExists(atPath: data.appending(path: "Engine/gone").path))
		#expect(!fileManager.fileExists(atPath: caches.appending(path: "Engine/gone").path))
		#expect(fileManager.fileExists(atPath: data.appending(path: "Engine/kept").path))
		#expect(fileManager.fileExists(atPath: root.appending(path: "outside").path))
		#expect(deletions.pending.isEmpty)
		#expect(!fileManager.fileExists(atPath: data.appending(path: "deletions.json").path))
	}
}
