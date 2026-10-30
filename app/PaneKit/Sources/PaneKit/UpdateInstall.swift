import Foundation

// Where an update installs itself (ADR 0092): over the running app's bundle,
// swapped in whole for a copy staged beside it. Pane replaces only a bundle it
// runs from in place: not a copy macOS translocated to a read-only path, and
// not one on a disk image.
public enum UpdateInstall {
	// The bundle to replace, or nil when the install is left to the user.
	public static func target(bundlePath: String) -> String? {
		guard bundlePath.hasSuffix(".app"), !bundlePath.contains("/AppTranslocation/"), !bundlePath.hasPrefix("/Volumes/") else {
			return nil
		}

		return bundlePath
	}

	// The new version's copy, beside the app on the same volume so one rename
	// swaps them, and hidden, so Finder and Launchpad never show it.
	public static func stagedName(version: String) -> String {
		".Pane-\(version).app"
	}

	// What an install leaves behind: a copy staged and never swapped in, or an
	// old bundle swapped out.
	public static func isLeftover(_ name: String) -> Bool {
		name.hasPrefix(".Pane-") && name.hasSuffix(".app")
	}

	private static let mountPrefix = "pane-update-"

	// Where a dmg mounts, in the temporary folder, while the Pane in it is
	// checked and copied: named for the process that mounted it.
	public static func mountName(pid: Int32) -> String {
		"\(mountPrefix)\(pid)-\(UUID().uuidString)"
	}

	// Whether `name` is such a mount point whose process is gone (`isRunning`
	// asks about a pid): a Pane that quit while staging, its dmg perhaps
	// still attached there. One named before 0.13.1, without a pid, is left
	// either way.
	public static func isLeftoverMount(_ name: String, isRunning: (Int32) -> Bool) -> Bool {
		guard name.hasPrefix(mountPrefix) else {
			return false
		}

		let rest = name.dropFirst(mountPrefix.count)

		if UUID(uuidString: String(rest)) != nil {
			return true
		}

		let parts = rest.split(separator: "-", maxSplits: 1)

		guard parts.count == 2, let pid = Int32(parts[0]), UUID(uuidString: String(parts[1])) != nil else {
			return false
		}

		return !isRunning(pid)
	}
}
