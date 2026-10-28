import AppKit
import PaneCore

// An extension as the app shows it: a copy of the engine's snapshot.
struct ExtensionInfo: Identifiable {
	let id: String
	let name: String
	let version: String
	let summary: String
	let enabled: Bool
	let pinned: Bool
	let icon: NSImage?
	let optionsURL: URL?
	let hasAction: Bool
	let actionTitle: String
	let badgeText: String
	let badgeBackground: NSColor?
	let badgeTextColor: NSColor?
	let actionEnabled: Bool
	let hasPopup: Bool

	@MainActor
	init(_ source: Extension) {
		id = source.identifier
		name = source.name
		version = source.version
		summary = source.summary
		enabled = source.isEnabled
		pinned = source.isPinned
		icon = source.icon
		optionsURL = source.optionsURL
		hasAction = source.hasAction
		actionTitle = source.actionTitle
		badgeText = source.badgeText
		badgeBackground = source.badgeBackgroundColor
		badgeTextColor = source.badgeTextColor
		actionEnabled = source.isActionEnabled
		hasPopup = source.hasPopup
	}
}
