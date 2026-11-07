import Foundation

// The app-wide appearance preference (CONTEXT.md: Theme). One knob: the app,
// its menus, and every page's `prefers-color-scheme` follow it.
public enum Theme: String, Codable, CaseIterable, Sendable {
	case system
	case light
	case dark
}

// The icon the Dock shows for Pane (CONTEXT.md: App icon): the window with one
// of ten skies, or automatic (ADR 0089). In Settings' order, the sky through
// a day.
public enum AppIcon: String, Codable, CaseIterable, Sendable {
	case automatic
	case day
	case clouds
	case morning
	case dusk
	case fuchsia
	case sunset
	case twilight
	case afterglow
	case night
	case moonrise

	// The window shown: automatic takes Day under a light appearance and Night
	// under a dark one, the app's own (Theme) or, for system, the Mac's.
	public func resolved(dark: Bool) -> AppIcon {
		self == .automatic ? (dark ? .night : .day) : self
	}
}

// The sidebar's width, which its edge resizes: 260 points by default (Aside's),
// between 200 and 400.
public enum SidebarWidth {
	public static let standard = 260.0
	public static let range = 200.0...400.0

	public static func clamped(_ width: Double) -> Double {
		min(max(width, range.lowerBound), range.upperBound)
	}
}

// Settings persisted in settings.json. A file written before a field existed
// keeps that field's default, while an explicit null still means "off".
public struct AppSettings: Codable, Hashable, Sendable {
	public static let defaults = AppSettings()

	public var theme: Theme
	public var appIcon: AppIcon
	// Minutes a background tab stays loaded before it sleeps; nil = off.
	public var tabSleepAfterMinutes: Int?
	// Minutes a hidden profile stays loaded before it unloads; nil = off.
	public var profileUnloadAfterMinutes: Int?
	// Megabytes of cache a profile may hold before an unload trims it; nil = off.
	public var cacheBudgetMB: Int?
	// Minutes without use before Pane locks, with a PIN set; nil = never by
	// time (it still locks when the Mac sleeps or locks, and on Lock Pane).
	public var lockAfterMinutes: Int?
	// Points, within SidebarWidth.range.
	public var sidebarWidth: Double

	private enum CodingKeys: String, CodingKey {
		case theme, appIcon, tabSleepAfterMinutes, profileUnloadAfterMinutes, cacheBudgetMB, lockAfterMinutes, sidebarWidth
	}

	public init(
		theme: Theme = .system,
		appIcon: AppIcon = .automatic,
		tabSleepAfterMinutes: Int? = 15,
		profileUnloadAfterMinutes: Int? = 30,
		cacheBudgetMB: Int? = 300,
		lockAfterMinutes: Int? = 15,
		sidebarWidth: Double = SidebarWidth.standard
	) {
		self.theme = theme
		self.appIcon = appIcon
		self.tabSleepAfterMinutes = tabSleepAfterMinutes
		self.profileUnloadAfterMinutes = profileUnloadAfterMinutes
		self.cacheBudgetMB = cacheBudgetMB
		self.lockAfterMinutes = lockAfterMinutes
		self.sidebarWidth = SidebarWidth.clamped(sidebarWidth)
	}

	public init(from decoder: Decoder) throws {
		let container = try decoder.container(keyedBy: CodingKeys.self)
		let defaults = AppSettings.defaults

		// Absent: the default. An explicit null: off.
		func optionalInt(_ key: CodingKeys, default value: Int?) throws -> Int? {
			guard container.contains(key) else {
				return value
			}

			return try container.decodeNil(forKey: key) ? nil : container.decode(Int.self, forKey: key)
		}

		theme = try container.decodeIfPresent(Theme.self, forKey: .theme) ?? defaults.theme
		appIcon = try container.decodeIfPresent(AppIcon.self, forKey: .appIcon) ?? defaults.appIcon
		tabSleepAfterMinutes = try optionalInt(.tabSleepAfterMinutes, default: defaults.tabSleepAfterMinutes)
		profileUnloadAfterMinutes = try optionalInt(.profileUnloadAfterMinutes, default: defaults.profileUnloadAfterMinutes)
		cacheBudgetMB = try optionalInt(.cacheBudgetMB, default: defaults.cacheBudgetMB)
		lockAfterMinutes = try optionalInt(.lockAfterMinutes, default: defaults.lockAfterMinutes)
		sidebarWidth = SidebarWidth.clamped(try container.decodeIfPresent(Double.self, forKey: .sidebarWidth) ?? defaults.sidebarWidth)
	}

	public func encode(to encoder: Encoder) throws {
		var container = encoder.container(keyedBy: CodingKeys.self)
		try container.encode(theme, forKey: .theme)
		try container.encode(appIcon, forKey: .appIcon)
		try container.encode(tabSleepAfterMinutes, forKey: .tabSleepAfterMinutes)
		try container.encode(profileUnloadAfterMinutes, forKey: .profileUnloadAfterMinutes)
		try container.encode(cacheBudgetMB, forKey: .cacheBudgetMB)
		try container.encode(lockAfterMinutes, forKey: .lockAfterMinutes)
		try container.encode(sidebarWidth, forKey: .sidebarWidth)
	}
}
