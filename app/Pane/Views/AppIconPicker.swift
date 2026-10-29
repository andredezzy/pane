import PaneKit
import SwiftUI

// The App icon row's control (ADR 0089): the chosen icon alone, a button that
// opens the picker. Hover fills behind it, as behind the row's other controls.
struct AppIconButton: View {
	let icon: AppIcon
	let action: () -> Void

	@State private var isHovered = false

	var body: some View {
		Button(action: action) {
			Image(nsImage: DockIcon.thumbnails[icon]!)
				.resizable()
				.frame(width: 36, height: 36)
				.padding(2)
				.background(Palette.accent.opacity(isHovered ? 1 : 0), in: Squircle(cornerRadius: 11))
				.contentShape(Squircle(cornerRadius: 11))
		}
		.buttonStyle(.plain)
		.onHover { hovering in
			withAnimation(Motion.hover) { isHovered = hovering }
		}
		.accessibilityLabel("App icon")
		.accessibilityValue(icon.rawValue.capitalized)
		.accessibilityHint("Shows every icon")
		.accessibilityIdentifier("pane.settings.appIcon")
	}
}

// Every sky to pick from, over the window as Quit Pane? is: the title, what
// Automatic does, then the icons six a row, the chosen one ringed, and Done. A
// click applies a sky at once and the Dock follows, so the picker stays open
// to compare; Done, Return, Esc, or a click outside closes it (KeyRouting).
struct AppIconPicker: View {
	let selection: AppIcon
	let select: (AppIcon) -> Void
	let done: () -> Void

	var body: some View {
		VStack(alignment: .leading, spacing: 16) {
			VStack(alignment: .leading, spacing: 6) {
				Text("App icon")
					.font(.system(size: 18, weight: .semibold))
					.tracking(-0.18)
					.foregroundStyle(Palette.foreground)
					.lineBox(24)

				Text("Automatic follows the theme: Day when light, Night when dark.")
					.font(.system(size: 14))
					// text-sm: 20px lines, half the leading above the first
					// and below the last.
					.lineSpacing(3)
					.padding(.vertical, 1.5)
					.foregroundStyle(Palette.foreground.opacity(0.7))
					.fixedSize(horizontal: false, vertical: true)
			}

			LazyVGrid(columns: Array(repeating: GridItem(.fixed(AppIconTile.width), spacing: 4), count: 6), alignment: .leading, spacing: 4) {
				ForEach(AppIcon.allCases, id: \.self) { icon in
					AppIconTile(icon: icon, isSelected: icon == selection) {
						select(icon)
					}
				}
			}

			HStack {
				Spacer()

				Button(action: done) {
					KeyedLabel(title: "Done", key: "↩", role: .primary)
				}
				.buttonStyle(.keyed(.primary))
				.accessibilityIdentifier("pane.appIcon.done")
			}
			.padding(.top, 8)
		}
		.padding(24)
		.frame(width: 480, alignment: .leading)
		.surfaceBackground(cornerRadius: 29, shadow: Palette.dialogShadow, blur: 64, y: 24)
		.accessibilityElement(children: .contain)
		.accessibilityAddTraits(.isModal)
		.accessibilityIdentifier("pane.appIcon")
	}
}

// One choice: its icon over its name. The chosen one is ringed as the profile
// sheet's swatch is (DESIGN.md › Shape), 2 to 4 points out from the icon's
// body, whose corners are the system's: continuous, 0.257 of its side
// (measured on macOS 27). The ring is drawn over the icon, so its shadow
// leaves it even. Hover fills the tile; choosing is instant.
private struct AppIconTile: View {
	static let width: CGFloat = 68
	// The icon at 56 points; macOS lays an app icon's body out at 824 of 1024.
	private static let size: CGFloat = 56
	private static let side = size * 824 / 1024
	private static let radius = side * 0.257

	let icon: AppIcon
	let isSelected: Bool
	let action: () -> Void

	@State private var isHovered = false

	var body: some View {
		Button(action: action) {
			VStack(spacing: 6) {
				Image(nsImage: DockIcon.thumbnails[icon]!)
					.resizable()
					.frame(width: Self.size, height: Self.size)
					.overlay {
						if isSelected {
							RoundedRectangle(cornerRadius: Self.radius + 4, style: .continuous)
								.strokeBorder(Palette.foreground, lineWidth: 2)
								.frame(width: Self.side + 8, height: Self.side + 8)
						}
					}

				Text(icon.rawValue.capitalized)
					.font(.system(size: 11))
					.foregroundStyle(isSelected ? Palette.foreground : Palette.mutedForeground)
					.lineLimit(1)
					.lineBox(16)
			}
			.frame(width: Self.width)
			.padding(.vertical, 8)
			.background(Palette.accent.opacity(isHovered ? 1 : 0), in: Squircle(cornerRadius: 15))
			.contentShape(Squircle(cornerRadius: 15))
		}
		.buttonStyle(.plain)
		.onHover { hovering in
			withAnimation(Motion.hover) { isHovered = hovering }
		}
		.accessibilityLabel(icon.rawValue.capitalized)
		.accessibilityAddTraits(isSelected ? .isSelected : [])
		.accessibilityIdentifier("pane.settings.appIcon.\(icon.rawValue)")
	}
}
