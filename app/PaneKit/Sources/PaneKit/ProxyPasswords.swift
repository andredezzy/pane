// Proxy passwords are kept apart from profiles.json, in a secret store the
// app provides (sealed under the engine's key, ProxyPasswordVault), by
// profile id. profiles.json writes them as null; one the file still holds
// (the Electron app's, or an older Pane's) is read as it is, then moved into
// the store.
public enum ProxyPasswords {
	// Each proxy's password, by profile id: what the store holds.
	public static func of(_ profiles: [BrowserProfile]) -> [String: String] {
		profiles.reduce(into: [:]) { passwords, profile in
			if let password = profile.proxy?.password, !password.isEmpty {
				passwords[profile.id] = password
			}
		}
	}

	// The profiles with their passwords from `stored`. A password still in
	// the file wins: it has not been moved yet.
	public static func filled(_ profiles: [BrowserProfile], from stored: [String: String]) -> [BrowserProfile] {
		profiles.map { profile in
			var profile = profile

			if profile.proxy != nil, profile.proxy?.password == nil {
				profile.proxy?.password = stored[profile.id]
			}

			return profile
		}
	}

	// The profiles as profiles.json keeps them: without passwords.
	public static func stripped(_ profiles: [BrowserProfile]) -> [BrowserProfile] {
		profiles.map { profile in
			var profile = profile
			profile.proxy?.password = nil

			return profile
		}
	}

	// Whether any password needs the store: one to read for a proxy that
	// signs in, or one to move out of the file.
	public static func needsStore(_ profiles: [BrowserProfile]) -> Bool {
		profiles.contains { !($0.proxy?.username ?? "").isEmpty || $0.proxy?.password != nil }
	}
}
