import SwiftUI

// The active profile's extension actions in the toolbar: the pinned ones as
// buttons with their badge, then the puzzle, which opens the extensions menu
// (ExtensionsMenu) and shows whether or not any are installed.
struct ExtensionActionsView: View {
	let controller: BrowserController

	private var model: BrowserModel { controller.model }
	private var pinned: [ExtensionInfo] {
		model.activeProfileID.flatMap { model.extensions[$0] }?.filter { $0.enabled && $0.hasAction && $0.pinned } ?? []
	}

	var body: some View {
		HStack(spacing: 5) {
			ForEach(pinned) { item in
				ActionButton(item: item) { controller.runExtensionAction(item.id) }
					.contextMenu { ActionContextMenu(controller: controller, item: item) }
					.anchorPreference(item.id)
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

// A pinned action's right-click menu, native as Aside's: its Web Store
// page, Remove and Unpin, then Manage extension and its Options.
private struct ActionContextMenu: View {
	let controller: BrowserController
	let item: ExtensionInfo

	var body: some View {
		Button { controller.openExtensionStorePage(item.id) } label: {
			MenuItemLabel(item.name, image: Lucide.squareArrowOutUpRight.menuImage)
		}

		Divider()

		Button {
			controller.model.activeProfileID.map { controller.confirmUninstall(item, inProfile: $0) }
		} label: {
			MenuItemLabel("Remove from Pane", image: Lucide.trash2.menuImage)
		}

		Button {
			controller.model.activeProfileID.map { controller.setExtension(item.id, pinned: false, inProfile: $0) }
		} label: {
			MenuItemLabel("Unpin", image: Glyph.pinOff.image(strokeWidth: 1.5, size: 16))
		}

		Divider()

		Button { controller.showExtensionSettings(extensionID: item.id) } label: {
			MenuItemLabel("Manage extension", image: Lucide.settings.menuImage)
		}

		if item.optionsURL != nil {
			Button { controller.openExtensionOptions(item) } label: {
				MenuItemLabel("Options", image: Lucide.settings.menuImage)
			}
		}
	}
}

private struct MenuItemLabel: View {
	let title: String
	let image: NSImage

	init(_ title: String, image: NSImage) {
		self.title = title
		self.image = image
	}

	var body: some View {
		Label {
			Text(title)
		} icon: {
			Image(nsImage: image)
		}
		// Menus show only a label's title unless asked for both.
		.labelStyle(.titleAndIcon)
	}
}

// The puzzle, as Aside's toolbar draws it, at the action buttons' size:
// muted; the accent fill on hover, and while its menu is open.
private struct ExtensionsButton: View {
	let isOpen: Bool
	let action: () -> Void

	@State private var isHovered = false
	@Environment(\.accessibilityReduceMotion) private var reduceMotion

	var body: some View {
		Button(action: action) {
			Image(nsImage: Glyph.puzzle.image(strokeWidth: 2, size: 20))
				.renderingMode(.template)
				.frame(width: 28, height: 28)
				.contentShape(Rectangle())
		}
		.buttonStyle(.plain)
		.foregroundStyle(isOpen ? Palette.foreground : isHovered ? Palette.accentForeground : Palette.mutedForeground)
		.background(isOpen || isHovered ? Palette.accent : .clear, in: Squircle(cornerRadius: 7))
		.onHover { hovering in
			withAnimation(reduceMotion ? nil : .easeOut(duration: 0.15)) { isHovered = hovering }
		}
		.help("Extensions")
		.accessibilityLabel("Extensions")
		.accessibilityAddTraits(isOpen ? .isSelected : [])
		.accessibilityIdentifier("pane.extensions")
	}
}

// The Electron app's action button (electron-chrome-extensions): 28px,
// rounded 7, the icon at 70%, a faint white wash on hover; no icon shows the
// name's first letter on gray. The badge is 12px, 9px text, bottom right.
private struct ActionButton: View {
	let item: ExtensionInfo
	let action: () -> Void

	@State private var isHovered = false

	var body: some View {
		Button(action: action) {
			Group {
				if let icon = item.icon {
					Image(nsImage: icon)
						.resizable()
						.interpolation(.high)
						.frame(width: 19.6, height: 19.6)
				} else {
					Text(item.name.prefix(1).uppercased())
						.font(.system(size: 11.2))
						.foregroundStyle(.white)
						.frame(width: 22.4, height: 22.4)
						.background(Color(rgb: 0x757575), in: Squircle(cornerRadius: 7))
				}
			}
			.frame(width: 28, height: 28)
			.background(isHovered ? Color.white.opacity(0.3) : .clear, in: Squircle(cornerRadius: 7))
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
			.contentShape(Rectangle())
		}
		.buttonStyle(.plain)
		.opacity(item.actionEnabled ? 1 : 0.4)
		.onHover { isHovered = $0 }
		.help(item.actionTitle.isEmpty ? item.name : item.actionTitle)
		.accessibilityLabel(item.actionTitle.isEmpty ? item.name : item.actionTitle)
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
