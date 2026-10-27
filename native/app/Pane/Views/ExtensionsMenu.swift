import SwiftUI

// The extensions menu, as Aside's (Paper: 02.9 Browser · Extensions, 03.6
// Extensions menu): 300 wide under the toolbar's puzzle, Manage all
// extensions and the Chrome Web Store, then the active profile's extensions.
// A row runs its extension's action; its pin (blue while pinned) keeps the
// menu open; its more button opens a native menu.
struct ExtensionsMenu: View {
	let controller: BrowserController

	private var model: BrowserModel { controller.model }
	private var extensions: [ExtensionInfo] {
		model.activeProfileID.flatMap { model.extensions[$0] }?.filter(\.enabled) ?? []
	}

	var body: some View {
		VStack(spacing: 0) {
			VStack(spacing: 0) {
				MenuRow(identifier: "pane.extensionsMenu.manage", action: { controller.showExtensionSettings() }) {
					RowIcon(glyph: .puzzle, strokeWidth: 2)
					RowTitle(text: "Manage all extensions")
				}

				MenuRow(identifier: "pane.extensionsMenu.webStore", action: controller.visitWebStore) {
					RowIcon(glyph: .download)
					RowTitle(text: "Visit Chrome Web Store")
				}
			}
			.padding(4)

			Rectangle()
				.fill(Palette.border)
				.frame(height: 1)

			VStack(alignment: .leading, spacing: 0) {
				Text("Installed extensions")
					.font(.system(size: 12))
					.foregroundStyle(Palette.mutedForeground)
					.padding(.horizontal, 6)
					.frame(height: 24)
					.accessibilityAddTraits(.isHeader)

				if extensions.isEmpty {
					Text("No installed extensions")
						.font(.system(size: 13))
						.foregroundStyle(Palette.mutedForeground)
						.padding(.horizontal, 8)
						.frame(height: 32)
				} else {
					ForEach(extensions) { item in
						ExtensionRow(controller: controller, item: item)
					}
				}
			}
			.frame(maxWidth: .infinity, alignment: .leading)
			.padding(4)
		}
		.frame(width: 300)
		// shadow-2xl (0 25px 50px -12px black 25%), on the surface alone: on
		// the whole view, SwiftUI shadows each row, text, and fill inside it.
		.background {
			RoundedRectangle(cornerRadius: 12, style: .circular)
				.fill(Palette.popover)
				.shadow(color: .black.opacity(0.25), radius: 25, y: 25)
		}
		.overlay {
			RoundedRectangle(cornerRadius: 12, style: .circular)
				.strokeBorder(Palette.foreground.opacity(0.1), lineWidth: 1)
		}
		.accessibilityElement(children: .contain)
		.accessibilityLabel("Extensions")
		.accessibilityIdentifier("pane.extensionsMenu")
	}
}

// An extension: its icon and name run its action (none without one); the
// pin and more buttons sit at the trailing edge, 24 points each.
private struct ExtensionRow: View {
	let controller: BrowserController
	let item: ExtensionInfo

	var body: some View {
		MenuRow(
			identifier: "pane.extensionsMenu.extension.\(item.id)",
			action: item.hasAction ? { controller.runExtensionFromMenu(item.id) } : nil
		) {
			Group {
				if let icon = item.icon {
					Image(nsImage: icon)
						.resizable()
						.interpolation(.high)
				} else {
					Text(item.name.prefix(1).uppercased())
						.font(.system(size: 10, weight: .medium))
						.foregroundStyle(.white)
						.frame(width: 16, height: 16)
						.background(Color(rgb: 0x757575), in: RoundedRectangle(cornerRadius: 4, style: .circular))
				}
			}
			.frame(width: 16, height: 16)
			.opacity(item.hasAction && !item.actionEnabled ? 0.4 : 1)
			.accessibilityHidden(true)

			RowTitle(text: item.name)
		} trailing: {
			if item.hasAction, let profileID = controller.model.activeProfileID {
				TrailingButton(label: item.pinned ? "Unpin from toolbar" : "Pin to toolbar") {
					controller.setExtension(item.id, pinned: !item.pinned, inProfile: profileID)
				} content: {
					Image(nsImage: (item.pinned ? Glyph.pinOff : Glyph.pin).image(strokeWidth: 1.75, size: 16))
						.renderingMode(.template)
						.foregroundStyle(item.pinned ? Self.pinnedColor : Palette.mutedForeground)
				}
				.accessibilityAddTraits(item.pinned ? .isSelected : [])
				.accessibilityIdentifier("pane.extensionsMenu.pin.\(item.id)")
			}

			// Its frame, for the native menu to open under.
			GeometryReader { geometry in
				TrailingButton(label: "More") {
					controller.showExtensionMoreMenu(item, at: geometry.frame(in: .global))
				} content: {
					Image(nsImage: Glyph.ellipsis.image(strokeWidth: 1.75, size: 16))
						.renderingMode(.template)
						.foregroundStyle(Palette.mutedForeground)
				}
				.accessibilityIdentifier("pane.extensionsMenu.more.\(item.id)")
			}
			.frame(width: 24, height: 24)
		}
	}

	// Aside's pinned blue: the progress blue, lighter on dark.
	private static let pinnedColor = Color(nsColor: NSColor(name: nil) { appearance in
		NSColor(rgb: Palette.isDark(appearance) ? 0x60A5FA : 0x3B82F6)
	})
}

// A 32-point row: a button from its leading edge to its trailing buttons,
// 8 points in, the whole row taking the accent fill on hover (rounded 8,
// inside the menu's 12 and 4 of padding). Without an action, the row only
// shows.
private struct MenuRow<Label: View, Trailing: View>: View {
	let identifier: String
	let action: (() -> Void)?
	@ViewBuilder let label: Label
	@ViewBuilder let trailing: Trailing

	@State private var isHovered = false
	@Environment(\.accessibilityReduceMotion) private var reduceMotion

	init(
		identifier: String,
		action: (() -> Void)?,
		@ViewBuilder label: () -> Label,
		@ViewBuilder trailing: () -> Trailing = { EmptyView() }
	) {
		self.identifier = identifier
		self.action = action
		self.label = label()
		self.trailing = trailing()
	}

	var body: some View {
		HStack(spacing: 0) {
			Button {
				action?()
			} label: {
				HStack(spacing: 8) {
					label
				}
				.padding(.leading, 8)
				.frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .leading)
				.contentShape(Rectangle())
			}
			.buttonStyle(.plain)
			.allowsHitTesting(action != nil)
			.accessibilityIdentifier(identifier)

			trailing
		}
		.padding(.trailing, 8)
		.frame(height: 32)
		.background(isHovered && action != nil ? Palette.accent : .clear, in: RoundedRectangle(cornerRadius: 8, style: .circular))
		.onHover { hovering in
			withAnimation(reduceMotion ? nil : .easeOut(duration: 0.15)) { isHovered = hovering }
		}
	}
}

// A row's 16-point icon, muted.
private struct RowIcon: View {
	let glyph: Glyph
	var strokeWidth = 1.75

	var body: some View {
		Image(nsImage: glyph.image(strokeWidth: strokeWidth, size: 16))
			.renderingMode(.template)
			.foregroundStyle(Palette.mutedForeground)
			.accessibilityHidden(true)
	}
}

// A row's 13-point title, cut short at the trailing buttons.
private struct RowTitle: View {
	let text: String

	var body: some View {
		Text(text)
			.font(.system(size: 13))
			.foregroundStyle(Palette.accentForeground)
			.lineLimit(1)
			.truncationMode(.tail)
			.frame(maxWidth: .infinity, alignment: .leading)
	}
}

// Pin and more: 24 points, a faint fill of their own on hover.
private struct TrailingButton<Content: View>: View {
	let label: String
	let action: () -> Void
	@ViewBuilder let content: Content

	@State private var isHovered = false
	@Environment(\.accessibilityReduceMotion) private var reduceMotion

	var body: some View {
		Button(action: action) {
			content
				.frame(width: 24, height: 24)
				.contentShape(Rectangle())
		}
		.buttonStyle(.plain)
		.background(isHovered ? Palette.foreground.opacity(0.08) : .clear, in: RoundedRectangle(cornerRadius: 6, style: .circular))
		.onHover { hovering in
			withAnimation(reduceMotion ? nil : .easeOut(duration: 0.15)) { isHovered = hovering }
		}
		.help(label)
		.accessibilityLabel(label)
	}
}
