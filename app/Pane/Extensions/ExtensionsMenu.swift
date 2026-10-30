import SwiftUI

// The extensions menu, as Aside's (Paper: 03.9 Browser · Extensions, 04.6
// Extensions menu): 300 wide under the toolbar's puzzle, Manage all
// extensions and the Chrome Web Store, then the active profile's extensions.
// A row runs its extension's action; its pin (blue while pinned) keeps the
// menu open; its more button opens ExtensionMoreMenu beside it.
struct ExtensionsMenu: View {
	let controller: ExtensionsController

	private var extensions: [ExtensionInfo] {
		controller.active.filter(\.enabled)
	}

	var body: some View {
		VStack(spacing: 0) {
			VStack(spacing: 0) {
				MenuRow(identifier: "pane.extensionsMenu.manage", action: { controller.showPage() }) {
					RowIcon(image: Glyph.puzzle.image(strokeWidth: 1.75, size: 16))
					RowTitle(text: "Manage all extensions")
				}

				MenuRow(identifier: "pane.extensionsMenu.webStore", action: { controller.visitWebStore() }) {
					RowIcon(image: Glyph.download.image(strokeWidth: 1.75, size: 16))
					RowTitle(text: "Visit Chrome Web Store")
				}
			}
			// 4 inside the menu's 1-point ring, as CSS's p-1 within its border.
			.padding(5)

			Hairline()

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
			.padding(5)
		}
		.frame(width: 300)
		.popoverSurface(cornerRadius: 22)
		.accessibilityElement(children: .contain)
		.accessibilityLabel("Extensions")
		.accessibilityIdentifier("pane.extensionsMenu")
	}
}

// An extension's more menu, as Aside's (Paper: 03.10 Browser · Extension
// popups): 240 wide beside the extensions menu, level with the row. Its
// name, then its Web Store page; Remove and Pin or Unpin; Manage Extension
// (its Details), and Options when it has them.
struct ExtensionMoreMenu: View {
	let controller: ExtensionsController
	let item: ExtensionInfo

	var body: some View {
		VStack(alignment: .leading, spacing: 0) {
			VStack(alignment: .leading, spacing: 0) {
				Text(item.name)
					.font(.system(size: 12))
					.foregroundStyle(Palette.mutedForeground)
					.lineLimit(1)
					.padding(.horizontal, 6)
					.frame(height: 24)
					.accessibilityAddTraits(.isHeader)

				MenuRow(identifier: "pane.extensionMoreMenu.storePage", action: { controller.openStorePage(item.id) }) {
					RowIcon(image: Lucide.squareArrowOutUpRight.image(strokeWidth: 1.75, size: 16))
					RowTitle(text: item.name)
				}
			}
			.padding(5)

			Hairline()

			VStack(spacing: 0) {
				MenuRow(identifier: "pane.extensionMoreMenu.remove", action: { controller.confirmUninstall(item) }) {
					RowIcon(image: Lucide.trash2.image(strokeWidth: 1.75, size: 16))
					RowTitle(text: "Remove from Pane")
				}

				if item.hasAction {
					MenuRow(identifier: "pane.extensionMoreMenu.pin", action: { controller.togglePinned(item) }) {
						RowIcon(image: (item.pinned ? Glyph.pinOff : Glyph.pin).image(strokeWidth: 1.75, size: 16))
						RowTitle(text: item.pinned ? "Unpin" : "Pin")
					}
				}
			}
			.padding(5)

			Hairline()

			VStack(spacing: 0) {
				MenuRow(identifier: "pane.extensionMoreMenu.manage", action: { controller.showDetails(item.id) }) {
					RowIcon(image: Lucide.settings.image(strokeWidth: 1.75, size: 16))
					RowTitle(text: "Manage Extension")
				}

				if item.optionsURL != nil {
					MenuRow(identifier: "pane.extensionMoreMenu.options", action: { controller.openOptions(item) }) {
						RowIcon(image: Lucide.settings.image(strokeWidth: 1.75, size: 16))
						RowTitle(text: "Options")
					}
				}
			}
			.padding(5)
		}
		.frame(width: 240)
		.popoverSurface(cornerRadius: 22)
		.accessibilityElement(children: .contain)
		.accessibilityLabel(item.name)
		.accessibilityIdentifier("pane.extensionMoreMenu")
	}
}

// An extension: its icon and name run its action (none without one); the
// pin and more buttons sit at the trailing edge, 24 points each.
private struct ExtensionRow: View {
	let controller: ExtensionsController
	let item: ExtensionInfo

	var body: some View {
		MenuRow(
			identifier: "pane.extensionsMenu.extension.\(item.id)",
			action: item.hasAction ? { controller.runFromMenu(item.id) } : nil,
			isHighlighted: controller.moreMenu?.extensionID == item.id
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
						.background(Palette.letterTile, in: Squircle(cornerRadius: 7))
				}
			}
			.frame(width: 16, height: 16)
			.opacity(item.hasAction && !item.actionEnabled ? 0.4 : 1)
			.accessibilityHidden(true)

			RowTitle(text: item.name)
		} trailing: {
			if item.hasAction {
				TrailingButton(label: item.pinned ? "Unpin from toolbar" : "Pin to toolbar") {
					controller.togglePinned(item)
				} content: {
					Image(nsImage: (item.pinned ? Glyph.pinOff : Glyph.pin).image(strokeWidth: 1.75, size: 16))
						.renderingMode(.template)
						.foregroundStyle(item.pinned ? Palette.pinned : Palette.mutedForeground)
				}
				.accessibilityAddTraits(item.pinned ? .isSelected : [])
				.accessibilityIdentifier("pane.extensionsMenu.pin.\(item.id)")
			}

			// Its frame: the more menu opens level with the row, 4 points above
			// and below the button.
			GeometryReader { geometry in
				TrailingButton(label: "More") {
					controller.toggleMoreMenu(item.id, row: geometry.frame(in: .global).insetBy(dx: 0, dy: -4))
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

}

// A 32-point row: a button from its leading edge to its trailing buttons,
// 8 points in, the whole row taking the accent fill on hover (rounded 15,
// inside the menu's 22, its ring, and 4 of padding), and while its more menu
// is open. A long name stops 8 short of the trailing buttons.
// Without an action, the row only shows.
private struct MenuRow<Label: View, Trailing: View>: View {
	let identifier: String
	let action: (() -> Void)?
	let isHighlighted: Bool
	@ViewBuilder let label: Label
	@ViewBuilder let trailing: Trailing

	init(
		identifier: String,
		action: (() -> Void)?,
		isHighlighted: Bool = false,
		@ViewBuilder label: () -> Label,
		@ViewBuilder trailing: () -> Trailing = { EmptyView() }
	) {
		self.identifier = identifier
		self.action = action
		self.isHighlighted = isHighlighted
		self.label = label()
		self.trailing = trailing()
	}

	var body: some View {
		HStack(spacing: 8) {
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
		.hoverBackground(isSelected: isHighlighted, selectedFill: Palette.accent, hoverFill: action == nil ? .clear : Palette.accent, cornerRadius: 15)
	}
}

// A row's 16-point icon, muted.
private struct RowIcon: View {
	let image: NSImage

	var body: some View {
		Image(nsImage: image)
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

	var body: some View {
		Button(action: action) {
			content
		}
		// The content keeps its own color (the pin's blue).
		.buttonStyle(.icon(size: 24, shape: .squircle(11), fill: Palette.foreground.opacity(0.08)))
		.help(label)
		.accessibilityLabel(label)
	}
}
