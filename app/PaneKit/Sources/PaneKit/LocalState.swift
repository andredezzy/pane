import Foundation

// Chromium's Local State, at the root of the engine's user data dir. At
// launch Chromium opens the profile it names as used last
// (GetStartupProfile, chrome_browser_main.cc), and the ones open last as its
// fallback: Pane's profiles, whose data is in the volume the data key opens
// later (ADR 0113). Before the engine starts, Pane names its Default profile
// instead, which holds nothing of the user's, and no profiles open last: Pane
// restores its own session. Chromium sets them again as profiles open.
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

	// The file naming Default as used last, and no profiles open last; nil
	// when it does already.
	public static func startingOnDefaultProfile(_ data: Data) throws -> Data? {
		guard var state = try JSONSerialization.jsonObject(with: data) as? [String: Any] else {
			return nil
		}

		var profile = state["profile"] as? [String: Any] ?? [:]

		guard profile["last_used"] as? String != "Default" || !(profile["last_active_profiles"] as? [Any] ?? []).isEmpty else {
			return nil
		}

		profile["last_used"] = "Default"
		profile["last_active_profiles"] = [String]()
		state["profile"] = profile

		return try JSONSerialization.data(withJSONObject: state)
	}
}
