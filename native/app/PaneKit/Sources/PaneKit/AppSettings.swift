import Foundation

// The app-wide appearance preference (CONTEXT.md: Theme). One knob: the app,
// its menus, and every page's `prefers-color-scheme` follow it.
public enum Theme: String, Codable, CaseIterable, Sendable {
	case system
	case light
	case dark
}

// Settings persisted in settings.json. A file written before a field existed
// keeps that field's default, while an explicit null still means "off".
public struct AppSettings: Codable, Hashable, Sendable {
	public static let defaults = AppSettings()

	public var chromiumPath: String
	public var theme: Theme
	// Minutes a background tab stays loaded before it sleeps; nil = off.
	public var tabSleepAfterMinutes: Int?
	// Minutes a hidden profile stays loaded before it unloads; nil = off.
	public var profileUnloadAfterMinutes: Int?
	// Megabytes of cache a profile may hold before an unload trims it; nil = off.
	public var cacheBudgetMB: Int?

	private enum CodingKeys: String, CodingKey {
		case chromiumPath, theme, tabSleepAfterMinutes, profileUnloadAfterMinutes, cacheBudgetMB
	}

	public init(
		chromiumPath: String = "",
		theme: Theme = .system,
		tabSleepAfterMinutes: Int? = 15,
		profileUnloadAfterMinutes: Int? = 30,
		cacheBudgetMB: Int? = 300
	) {
		self.chromiumPath = chromiumPath
		self.theme = theme
		self.tabSleepAfterMinutes = tabSleepAfterMinutes
		self.profileUnloadAfterMinutes = profileUnloadAfterMinutes
		self.cacheBudgetMB = cacheBudgetMB
	}

	public init(from decoder: Decoder) throws {
		let container = try decoder.container(keyedBy: CodingKeys.self)
		let defaults = AppSettings.defaults

		func minutes(_ key: CodingKeys, default value: Int?) throws -> Int? {
			guard container.contains(key) else {
				return value
			}

			return try container.decodeNil(forKey: key) ? nil : container.decode(Int.self, forKey: key)
		}

		chromiumPath = try container.decodeIfPresent(String.self, forKey: .chromiumPath) ?? defaults.chromiumPath
		theme = try container.decodeIfPresent(Theme.self, forKey: .theme) ?? defaults.theme
		tabSleepAfterMinutes = try minutes(.tabSleepAfterMinutes, default: defaults.tabSleepAfterMinutes)
		profileUnloadAfterMinutes = try minutes(.profileUnloadAfterMinutes, default: defaults.profileUnloadAfterMinutes)
		cacheBudgetMB = try minutes(.cacheBudgetMB, default: defaults.cacheBudgetMB)
	}

	public func encode(to encoder: Encoder) throws {
		var container = encoder.container(keyedBy: CodingKeys.self)
		try container.encode(chromiumPath, forKey: .chromiumPath)
		try container.encode(theme, forKey: .theme)
		try container.encode(tabSleepAfterMinutes, forKey: .tabSleepAfterMinutes)
		try container.encode(profileUnloadAfterMinutes, forKey: .profileUnloadAfterMinutes)
		try container.encode(cacheBudgetMB, forKey: .cacheBudgetMB)
	}
}
