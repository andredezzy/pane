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
		let volume = DataVolume(dataDirectory: data, cacheDirectory: caches)
		defer { try? fileManager.removeItem(at: root) }

		// "gone" as an earlier Pane left it, plain; "linked" behind its links
		// into the volume.
		for folder in ["data/Engine/gone/Network", "caches/Engine/gone/Cache", "data/Engine/kept", "outside"] {
			try fileManager.createDirectory(at: root.appending(path: folder), withIntermediateDirectories: true)
		}

		try volume.prepare(profile: "linked")

		let deletions = ProfileDeletions(dataDirectory: data)
		deletions.add("gone")
		deletions.add("linked")
		deletions.add("confirmed")
		deletions.remove("confirmed")
		// Never a path out of Engine.
		deletions.add("../../outside")

		#expect(ProfileDeletions(dataDirectory: data).pending == ["gone", "linked", "../../outside"])
		#expect(deletions.purge(volume: volume).isEmpty)

		for gone in ["data/Engine/gone", "caches/Engine/gone", "data/Engine/linked", "caches/Engine/linked", "data/Engine/Volume/linked"] {
			#expect((try? fileManager.attributesOfItem(atPath: root.appending(path: gone).path)) == nil, "\(gone) is still there")
		}

		#expect(fileManager.fileExists(atPath: data.appending(path: "Engine/kept").path))
		#expect(fileManager.fileExists(atPath: root.appending(path: "outside").path))
		#expect(deletions.pending.isEmpty)
		#expect(!fileManager.fileExists(atPath: data.appending(path: "deletions.json").path))
	}
}
