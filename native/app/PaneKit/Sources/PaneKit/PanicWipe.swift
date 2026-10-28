import Foundation

// The panic wipe after the last failed PIN attempt (apps/veil
// security/wipe.ts): every profile's engine data and the app's own files are
// overwritten with random bytes, then deleted. Nothing outside them is
// touched. The data moves out of the app's way first (stage), at once, so a
// fresh Pane starts on nothing while the moved data is erased; a wipe cut
// short (a crash, a quit) is found and finished at the next launch.
public enum PanicWipe {
	// What it erases: the engine's data (every profile, extensions and all),
	// the app's JSON files, and the engine's caches when they live apart.
	// security.json comes last: while it stays, with its count of failed
	// attempts, the wipe is still due.
	public static func targets(dataDirectory: URL, cacheDirectory: URL?) -> [URL] {
		["Engine", "profiles.json", "settings.json", "deletions.json", "security.json"].map {
			dataDirectory.appending(path: $0)
		} + [cacheDirectory?.appending(path: "Engine")].compactMap(\.self)
	}

	// Chromium keeps a profile's caches under ~/Library/Caches when the
	// profile is under ~/Library/Application Support, at the same relative
	// path (chrome_paths_mac.mm GetUserCacheDirectory). nil otherwise.
	public static func cacheDirectory(for dataDirectory: URL, applicationSupport: URL, caches: URL) -> URL? {
		let data = dataDirectory.standardizedFileURL.pathComponents
		let support = applicationSupport.standardizedFileURL.pathComponents

		guard data.count > support.count, Array(data.prefix(support.count)) == support else {
			return nil
		}

		return data.dropFirst(support.count).reduce(caches) { $0.appending(path: $1) }
	}

	// What could not be erased, for the log and the security scenario.
	public struct Failure: Hashable, Sendable, CustomStringConvertible {
		public let path: String
		public let reason: String

		public var description: String {
			"\(path): \(reason)"
		}
	}

	// A folder of moved data, and how many random passes it gets: three for
	// the profiles' data, one for caches, which hold nothing the profile
	// does not.
	public struct Staged: Hashable, Sendable {
		public let url: URL
		public let passes: Int
	}

	// Where the moved data waits, beside each root on the same volume, so
	// the move is a rename.
	static let stagingName = "Wiping"

	// Moves every target into `Wiping/<id>/` beside it, in `targets`'s order.
	public static func stage(dataDirectory: URL, cacheDirectory: URL?) -> [Failure] {
		let fileManager = FileManager.default
		let id = UUID().uuidString
		var failures: [Failure] = []

		for target in targets(dataDirectory: dataDirectory, cacheDirectory: cacheDirectory)
			where (try? target.checkResourceIsReachable()) == true || isLink(target)
		{
			let folder = target.deletingLastPathComponent().appending(path: "\(stagingName)/\(id)", directoryHint: .isDirectory)

			do {
				try fileManager.createDirectory(at: folder, withIntermediateDirectories: true)
				try fileManager.moveItem(at: target, to: folder.appending(path: target.lastPathComponent))
			} catch {
				failures.append(Failure(path: target.path, reason: String(describing: error)))
			}
		}

		return failures
	}

	// Everything staged and not erased yet, this wipe's or one cut short's.
	public static func staged(dataDirectory: URL, cacheDirectory: URL?) -> [Staged] {
		[(dataDirectory, 3), (cacheDirectory, 1)].flatMap { root, passes -> [Staged] in
			guard let root else {
				return []
			}

			let staging = root.appending(path: stagingName, directoryHint: .isDirectory)
			let wipes = (try? FileManager.default.contentsOfDirectory(at: staging, includingPropertiesForKeys: nil)) ?? []

			return wipes.map { Staged(url: $0, passes: passes) }
		}
	}

	// Erases everything staged, then the staging folders once empty: a wipe
	// staged meanwhile keeps its own.
	public static func eraseStaged(dataDirectory: URL, cacheDirectory: URL?) -> [Failure] {
		let failures = staged(dataDirectory: dataDirectory, cacheDirectory: cacheDirectory).flatMap { erase($0.url, passes: $0.passes) }

		for root in [dataDirectory, cacheDirectory].compactMap(\.self) {
			rmdir(root.appending(path: stagingName).path)
		}

		return failures
	}

	// Overwrites and deletes `url` and everything under it. A symbolic link
	// goes, never what it points to: Chromium links its SingletonSocket into
	// the temporary directory.
	public static func erase(_ url: URL, passes: Int = 3) -> [Failure] {
		let fileManager = FileManager.default
		var failures: [Failure] = []

		guard let type = attributes(url, &failures)?[.type] as? FileAttributeType else {
			return failures
		}

		if type == .typeDirectory {
			do {
				for entry in try fileManager.contentsOfDirectory(at: url, includingPropertiesForKeys: nil) {
					failures += erase(entry, passes: passes)
				}
			} catch {
				failures.append(Failure(path: url.path, reason: String(describing: error)))
			}
		} else if type == .typeRegular {
			failures += overwrite(url, passes: passes)
		}

		do {
			try fileManager.removeItem(at: url)
		} catch {
			failures.append(Failure(path: url.path, reason: String(describing: error)))
		}

		return failures
	}

	private static func attributes(_ url: URL, _ failures: inout [Failure]) -> [FileAttributeKey: Any]? {
		do {
			return try FileManager.default.attributesOfItem(atPath: url.path)
		} catch CocoaError.fileReadNoSuchFile {
			// Gone already: nothing to erase.
			return nil
		} catch {
			failures.append(Failure(path: url.path, reason: String(describing: error)))
			return nil
		}
	}

	private static func isLink(_ url: URL) -> Bool {
		(try? FileManager.default.destinationOfSymbolicLink(atPath: url.path)) != nil
	}

	// Passes of random bytes over the whole file, each synced to disk.
	private static func overwrite(_ url: URL, passes: Int) -> [Failure] {
		do {
			let size = try url.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? 0

			guard size > 0 else {
				return []
			}

			let handle = try FileHandle(forWritingTo: url)
			defer { try? handle.close() }

			for _ in 0 ..< passes {
				var offset = 0
				try handle.seek(toOffset: 0)

				while offset < size {
					let count = min(1 << 20, size - offset)
					var bytes = [UInt8](repeating: 0, count: count)
					arc4random_buf(&bytes, count)
					try handle.write(contentsOf: bytes)
					offset += count
				}

				try handle.synchronize()
			}

			return []
		} catch {
			return [Failure(path: url.path, reason: String(describing: error))]
		}
	}
}
