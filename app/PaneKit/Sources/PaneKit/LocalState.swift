import Foundation

// Chromium's Local State, at the root of the engine's user data dir. At
// launch Chromium opens the profile it names as used last
// (GetStartupProfile, chrome_browser_main.cc), and the ones open last as its
// fallback: Pane's profiles, whose data is in the volume the data key opens
// later (ADR 0113). Before the engine starts, Pane names its Default profile
// instead, which holds nothing of the user's, and no profiles open last: Pane
// restores its own session. Chromium sets them again as profiles open, and
// writes them as it quits, with when each profile was used (active_time) and
// how many tabs were open (tab_stats): those go too, at launch and at quit,
// since the file is outside the volume.
public enum LocalState {
	public static func startOnDefaultProfile(userDataDirectory: URL) throws {
		let file = userDataDirectory.appending(path: "Local State")

		guard FileManager.default.fileExists(atPath: file.path) else {
			return
		}

		if let changed = try startingOnDefaultProfile(Data(contentsOf: file)) {
			try changed.write(to: file, options: .atomic)
		}
	}

	// The file naming Default as used last, no profiles open last, and no
	// time or count of use; nil when it says so already.
	public static func startingOnDefaultProfile(_ data: Data) throws -> Data? {
		guard var state = try JSONSerialization.jsonObject(with: data) as? [String: Any] else {
			return nil
		}

		var profile = state["profile"] as? [String: Any] ?? [:]
		var profiles = profile["info_cache"] as? [String: Any] ?? [:]
		let used = profiles.values.contains { ($0 as? [String: Any])?["active_time"] != nil }

		guard profile["last_used"] as? String != "Default" || !(profile["last_active_profiles"] as? [Any] ?? []).isEmpty
			|| used || state["tab_stats"] != nil
		else {
			return nil
		}

		for (id, entry) in profiles {
			if var entry = entry as? [String: Any] {
				entry.removeValue(forKey: "active_time")
				profiles[id] = entry
			}
		}

		profile["last_used"] = "Default"
		profile["last_active_profiles"] = [String]()

		if profile["info_cache"] != nil {
			profile["info_cache"] = profiles
		}

		state["profile"] = profile
		state.removeValue(forKey: "tab_stats")

		return try JSONSerialization.data(withJSONObject: state)
	}
}
