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
}
