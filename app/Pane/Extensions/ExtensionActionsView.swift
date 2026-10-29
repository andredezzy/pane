import PaneCore
import SwiftUI

// The active profile's extension actions in the toolbar: the pinned ones as
// buttons with their badge, then the puzzle, which opens the extensions menu
// (ExtensionsMenu) and shows whether or not any are installed. As Aside's,
// the pinned ones sit in a capsule that shows while the pointer is over them
// or one's popup is open.
struct ExtensionActionsView: View {
	let extensions: ExtensionsController
	// Whether a profile is active, so its extensions can show.
	let hasProfile: Bool

	@State private var isHovered = false

	private var pinned: [ExtensionInfo] {
		extensions.active.filter { $0.enabled && $0.hasAction && $0.pinned }
	}

	var body: some View {
		HStack(spacing: 4) {
			if !pinned.isEmpty {
				let openID = extensions.popover.popup?.extensionIdentifier

				HStack(spacing: 4) {
					ForEach(pinned) { item in
						ActionButton(item: item, isOpen: openID == item.id) {
							extensions.runAction(item.id)
						}
						.contextMenu { ActionContextMenu(extensions: extensions, item: item) }
						.anchorPreference(item.id)
					}
				}
				.padding(1)
				.overlay {
					Capsule(style: .circular)
						.strokeBorder(Palette.border, lineWidth: 1)
						.opacity(isHovered || pinned.contains { $0.id == openID } ? 1 : 0)
				}
				.onHover { hovering in
					withAnimation(Motion.hover) { isHovered = hovering }
				}
			}

			if hasProfile {
				ExtensionsButton(isOpen: extensions.menuOpen, action: extensions.toggleMenu)
					.anchorPreference(ExtensionsController.menuAnchor)
			}
		}
		.onPreferenceChange(ActionAnchorKey.self) { anchors in
			extensions.actionAnchors = anchors
		}
	}
}

// A pinned action's right-click menu, native and plain as Aside's: its name
// (its Web Store page), the extension's own items for its action, Remove
// from Pane and Unpin, then Manage Extension. Aside also lists its site
// access, which needs the engine.
private struct ActionContextMenu: View {
	let extensions: ExtensionsController
	let item: ExtensionInfo

	var body: some View {
		Button(item.name) { extensions.openStorePage(item.id) }

		if let menu = extensions.actionMenu(item.id) {
			Divider()

			ExtensionMenuItems(items: menu.items, run: menu.run)
		}

		Divider()

		Button("Remove from Pane") { extensions.confirmUninstall(item) }

		Button("Unpin") { extensions.togglePinned(item) }

		Divider()

		Button("Manage Extension") { extensions.showPage(extensionID: item.id) }
	}
}

// An extension's items (chrome.contextMenus) in a SwiftUI menu: a checkbox
// or radio item as a toggle, which shows its check, and a submenu as a menu.
private struct ExtensionMenuItems: View {
	let items: [ExtensionMenuItem]
	let run: (ExtensionMenuItem) -> Void

	var body: some View {
		ForEach(items, id: \.self) { item in
			switch item.kind {
			case .separator:
				Divider()

			case .submenu:
				Menu(item.title) { ExtensionMenuItems(items: item.children, run: run) }
					.disabled(!item.isEnabled)

			case .checkbox, .radio:
				Toggle(item.title, isOn: Binding { item.isChecked } set: { _ in run(item) })
					.disabled(!item.isEnabled)

			default:
				Button(item.title) { run(item) }
					.disabled(!item.isEnabled)
			}
		}
	}
}

// The puzzle, as Aside's toolbar draws it, a 32-point circle as the action
// buttons: muted; the accent fill on hover, and while its menu is open.
private struct ExtensionsButton: View {
	let isOpen: Bool
	let action: () -> Void

	var body: some View {
		Button(action: action) {
			// 1.4 of 24 at 20 points: 1.17 points, the thin stroke the user
			// chose for it (the chevrons beside it draw Aside's 1.5).
			Image(nsImage: Glyph.puzzle.image(strokeWidth: 1.4, size: 20))
				.renderingMode(.template)
		}
		// Open, it takes the text's own color.
		.buttonStyle(.icon(size: 32, hoverTint: isOpen ? Palette.foreground : Palette.accentForeground, isOn: isOpen))
		.help("Extensions")
		.accessibilityLabel("Extensions")
		.accessibilityAddTraits(isOpen ? .isSelected : [])
		.accessibilityIdentifier("pane.extensions")
	}
}

// An extension's toolbar button, as Aside's: a 32-point circle, filled on
// hover and while its popup shows (and, the pointer still on it, while its
// right-click menu does); the icon at 16 points, as Chrome and Aside draw
// action icons; no icon shows the name's first letter on gray. The badge is
// 12px, 9px text, bottom right.
private struct ActionButton: View {
	let item: ExtensionInfo
	let isOpen: Bool
	let action: () -> Void

	var body: some View {
		Button(action: action) {
			Group {
				if let icon = item.icon {
					Image(nsImage: icon)
						.resizable()
						.interpolation(.high)
						.frame(width: 16, height: 16)
				} else {
					Text(item.name.prefix(1).uppercased())
						.font(.system(size: 9, weight: .medium))
						.foregroundStyle(.white)
						.frame(width: 16, height: 16)
						.background(Palette.letterTile, in: Squircle(cornerRadius: 4))
				}
			}
			.frame(width: 32, height: 32)
			.overlay(alignment: .bottomTrailing) {
				if !item.badgeText.isEmpty {
					Text(item.badgeText)
						.font(.system(size: 9))
						.lineLimit(1)
						.foregroundStyle(Color(nsColor: item.badgeTextColor ?? .white))
						.padding(.horizontal, 2)
						.frame(height: 12)
						.background(Color(nsColor: item.badgeBackground ?? .systemRed), in: Squircle(cornerRadius: 4))
						.overlay {
							Squircle(cornerRadius: 4).stroke(Palette.badgeRing, lineWidth: 1)
						}
						.padding(.bottom, 1)
				}
			}
		}
		.buttonStyle(.icon(size: 32, isOn: isOpen))
		.opacity(item.actionEnabled ? 1 : 0.4)
		.help(item.actionTitle.isEmpty ? item.name : item.actionTitle)
		.accessibilityLabel(item.actionTitle.isEmpty ? item.name : item.actionTitle)
		.accessibilityAddTraits(isOpen ? .isSelected : [])
		.accessibilityIdentifier("pane.extension.\(item.id)")
	}
}

// Each toolbar button's frame, keyed by extension ID, for popups to point at.
private struct ActionAnchorKey: PreferenceKey {
	static let defaultValue: [String: CGRect] = [:]

	static func reduce(value: inout [String: CGRect], nextValue: () -> [String: CGRect]) {
		value.merge(nextValue()) { $1 }
	}
}

private extension View {
	func anchorPreference(_ key: String) -> some View {
		background {
			GeometryReader { geometry in
				Color.clear.preference(key: ActionAnchorKey.self, value: [key: geometry.frame(in: .global)])
			}
		}
	}
}
