import Foundation

// Pane's identifier (ADR 0114), the reverse of its domain,
// pane.roundedcorners.xyz: its bundle's, its data root's and caches' folder
// names, its preferences' domain, and its log's subsystem. The engine's
// BRANDING (patch 0001) and app/project.yml name it too. Until 0.14.0 it was
// com.pane.desktop, whose data moves at the first launch after
// (moveFormerData).
public enum BundleIdentifier {
	public static let pane = "xyz.roundedcorners.pane"
	public static let former = "com.pane.desktop"

	// What an earlier Pane kept under its former identifier moves to this
	// one's, before anything reads it: the data root and the caches, a rename
	// each on the same volume, and the window's frame from its preferences,
	// whose domain then goes (it kept the open panel's last folder, ADR
	// 0113). Nothing moves once this identifier's data root exists. Returns
	// what could not move. `defaults` nil leaves the preferences: under a
	// fixed home (scenarios) the data is the run's, but the preferences are
	// the user's own, since cfprefsd ignores a fixed home. Tests name
	// identifiers of their own: a persistent domain is the whole Mac's,
	// whichever UserDefaults reads it.
	public static func moveFormerData(
		applicationSupport: URL,
		caches: URL,
		defaults: UserDefaults?,
		from former: String = former,
		to pane: String = pane
	) -> [String] {
		let fileManager = FileManager.default
		let data = applicationSupport.appending(path: pane, directoryHint: .isDirectory)
		let formerData = applicationSupport.appending(path: former, directoryHint: .isDirectory)

		guard !fileManager.fileExists(atPath: data.path), fileManager.fileExists(atPath: formerData.path) else {
			return []
		}

		var failures: [String] = []

		for (from, to) in [(formerData, data), (caches.appending(path: former), caches.appending(path: pane))]
			where fileManager.fileExists(atPath: from.path) && !fileManager.fileExists(atPath: to.path)
		{
			do {
				try fileManager.moveItem(at: from, to: to)
			} catch {
				failures.append("\(from.path): \(error)")
			}
		}

		if let defaults, let formerDefaults = defaults.persistentDomain(forName: former) {
			for (key, value) in formerDefaults where key.hasPrefix("NSWindow Frame ") {
				defaults.set(value, forKey: key)
			}

			defaults.removePersistentDomain(forName: former)
		}

		return failures
	}

	// What URLSession's default sessions kept for an earlier Pane, in the
	// clear and outside the encrypted data (ADR 0113): the update check's
	// responses (Cache.db, fsCachedData, under the caches' folder, which the
	// move above takes along) and its HTTP storage, under either identifier.
	// Pane's sessions are ephemeral since, so none of it comes back.
	public static func removeSessionCaches(library: URL, identifiers: [String] = [pane, former]) -> [String] {
		let fileManager = FileManager.default
		var failures: [String] = []

		for identifier in identifiers {
			let caches = library.appending(path: "Caches/\(identifier)", directoryHint: .isDirectory)
			let storages = library.appending(path: "HTTPStorages", directoryHint: .isDirectory)

			let leftovers = ["Cache.db", "Cache.db-shm", "Cache.db-wal", "fsCachedData"].map { caches.appending(path: $0) }
				+ [storages.appending(path: identifier), storages.appending(path: "\(identifier).binarycookies")]

			for leftover in leftovers where fileManager.fileExists(atPath: leftover.path) {
				do {
					try fileManager.removeItem(at: leftover)
				} catch {
					failures.append("\(leftover.path): \(error)")
				}
			}
		}

		return failures
	}

	// The user's cache folder (DARWIN_USER_CACHE_DIR, under /var/folders),
	// which the system names for each app: HOME does not move it.
	public static var userCacheDirectory: URL? {
		let length = confstr(_CS_DARWIN_USER_CACHE_DIR, nil, 0)

		guard length > 0 else {
			return nil
		}

		var buffer = [CChar](repeating: 0, count: length)

		guard confstr(_CS_DARWIN_USER_CACHE_DIR, &buffer, length) > 0 else {
			return nil
		}

		return URL(fileURLWithPath: String(decoding: buffer.prefix { $0 != 0 }.map(UInt8.init(bitPattern:)), as: UTF8.self), isDirectory: true)
	}

	// What the system kept there for the former identifier, which nothing
	// reads now: its URL sessions' responses (Cache.db, fsCachedData, 0.13's
	// update check among them) and its helpers' shader caches (ADR 0118).
	public static func removeFormerUserCaches(userCaches: URL, former: String = former) -> [String] {
		[former, "\(former).helper"].compactMap { name in
			let folder = userCaches.appending(path: name, directoryHint: .isDirectory)

			guard FileManager.default.fileExists(atPath: folder.path) else {
				return nil
			}

			do {
				try FileManager.default.removeItem(at: folder)
				return nil
			} catch {
				return "\(folder.path): \(error)"
			}
		}
	}
}
