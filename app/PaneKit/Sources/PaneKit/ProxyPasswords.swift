// The proxy passwords an earlier Pane kept apart from profiles.json, by
// profile id (ProxyPasswordVault), go back into its profiles when its plain
// profiles.json is read and sealed (ADR 0113). A password the file still
// holds (the Electron app's, or an older Pane's) is read as it is.
public enum ProxyPasswords {
	// The profiles with their passwords from `stored`. A password still in
	// the file wins: it was never moved.
	public static func filled(_ profiles: [BrowserProfile], from stored: [String: String]) -> [BrowserProfile] {
		profiles.map { profile in
			var profile = profile

			if profile.proxy != nil, profile.proxy?.password == nil {
				profile.proxy?.password = stored[profile.id]
			}

			return profile
		}
	}

	// Whether any password needs the store: one to read for a proxy that
	// signs in.
	public static func needsStore(_ profiles: [BrowserProfile]) -> Bool {
		profiles.contains { !($0.proxy?.username ?? "").isEmpty || $0.proxy?.password != nil }
	}
}
