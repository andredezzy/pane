import Foundation

// The panic wipe after the last failed PIN attempt (apps/veil
// security/wipe.ts): every profile's engine data and the app's own files are
// overwritten with random bytes, three times, then deleted. Nothing outside
// them is touched.
public enum PanicWipe {
	// What it erases: the engine's data (every profile, extensions and all),
	// the app's JSON files, and the engine's caches when they live apart.
	public static func targets(dataDirectory: URL, cacheDirectory: URL?) -> [URL] {
		["Engine", "profiles.json", "settings.json", "security.json"].map {
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

	public static func run(dataDirectory: URL, cacheDirectory: URL?) {
		for target in targets(dataDirectory: dataDirectory, cacheDirectory: cacheDirectory) {
			erase(target)
		}
	}

	// A symbolic link goes, never what it points to: Chromium links its
	// SingletonSocket into the temporary directory.
	private static func erase(_ url: URL) {
		guard let type = (try? FileManager.default.attributesOfItem(atPath: url.path))?[.type] as? FileAttributeType else {
			return
		}

		if type == .typeDirectory {
			let entries = (try? FileManager.default.contentsOfDirectory(at: url, includingPropertiesForKeys: nil)) ?? []

			for entry in entries {
				erase(entry)
			}
		} else if type == .typeRegular {
			overwrite(url)
		}

		try? FileManager.default.removeItem(at: url)
	}

	// Three passes of random bytes over the whole file, each synced to disk.
	private static func overwrite(_ url: URL, passes: Int = 3) {
		guard let handle = try? FileHandle(forWritingTo: url),
		      let size = try? url.resourceValues(forKeys: [.fileSizeKey]).fileSize, size > 0
		else {
			return
		}

		defer { try? handle.close() }

		for _ in 0 ..< passes {
			var offset = 0
			try? handle.seek(toOffset: 0)

			while offset < size {
				let count = min(1 << 20, size - offset)
				var bytes = [UInt8](repeating: 0, count: count)
				arc4random_buf(&bytes, count)
				try? handle.write(contentsOf: bytes)
				offset += count
			}

			try? handle.synchronize()
		}
	}
}
