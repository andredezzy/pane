import SwiftUI

// The active profile's extension actions in the toolbar: the pinned ones as
// buttons with their badge, then the puzzle, which opens the extensions menu
// (ExtensionsMenu) and shows whether or not any are installed. As Aside's,
// the pinned ones sit in a capsule that shows while the pointer is over them
// or one's popup is open.
struct ExtensionActionsView: View {
	let controller: BrowserController

	@State private var isHovered = false

	private var model: BrowserModel { controller.model }
	private var pinned: [ExtensionInfo] {
		model.activeProfileID.flatMap { model.extensions[$0] }?.filter { $0.enabled && $0.hasAction && $0.pinned } ?? []
	}

	var body: some View {
		HStack(spacing: 4) {
			if !pinned.isEmpty {
				let openID = controller.extensionPopover.popup?.extensionIdentifier

				HStack(spacing: 4) {
					ForEach(pinned) { item in
						ActionButton(item: item, isOpen: openID == item.id) {
							controller.runExtensionAction(item.id)
						}
						.contextMenu { ActionContextMenu(controller: controller, item: item) }
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

			if model.activeProfileID != nil {
				ExtensionsButton(isOpen: controller.state.extensionsMenuOpen, action: controller.toggleExtensionsMenu)
					.anchorPreference(WindowState.extensionsMenuAnchor)
			}
		}
		.onPreferenceChange(ActionAnchorKey.self) { anchors in
			controller.state.actionAnchors = anchors
		}
	}
}

// A pinned action's right-click menu, native and plain as Aside's: its name
// (its Web Store page), Remove from Pane and Unpin, then Manage Extension.
// Aside also lists the extension's own action items and its site access,
// which need the engine.
private struct ActionContextMenu: View {
	let controller: BrowserController
	let item: ExtensionInfo

	var body: some View {
		Button(item.name) { controller.openExtensionStorePage(item.id) }

		Divider()

		Button("Remove from Pane") {
			controller.model.activeProfileID.map { controller.confirmUninstall(item, inProfile: $0) }
		}

		Button("Unpin") {
			controller.model.activeProfileID.map { controller.setExtension(item.id, pinned: false, inProfile: $0) }
		}

		Divider()

		Button("Manage Extension") { controller.showExtensions(extensionID: item.id) }
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
						.background(Color(rgb: 0x757575), in: Squircle(cornerRadius: 5))
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
							Squircle(cornerRadius: 4).stroke(Color(rgb: 0x444444), lineWidth: 1)
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
