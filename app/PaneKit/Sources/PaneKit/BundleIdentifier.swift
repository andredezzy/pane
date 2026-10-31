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
}
