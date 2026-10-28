import SwiftUI

// A menu's trigger, as Aside's pop-up button and packages/ui's Select: the
// value (with its icon, if it has one) and a chevron, in a 1-point border,
// with no fill but the accent's on hover. A click shows the system's menu
// of the options, the current one checked.
struct PaneMenuButton<Value: Hashable>: View {
	enum Size {
		// Among a form's fields (h-9, at 14): the profile sheet's.
		case regular
		// Beside the small buttons (h-8, at 13): Settings', the Extensions
		// page's.
		case small
	}

	enum Chevron {
		// A select's single chevron down, 16 points.
		case down
		// Aside's pop-up button's up and down, 13 points.
		case upDown
	}

	let options: [(String, Value)]
	let selection: Value
	var size = Size.small
	var icon: ((Value) -> Lucide)?
	var chevron = Chevron.down
	// The Extensions page's takes the profile's color.
	var ring = Palette.border
	// Laid out at the width it is given, the chevron at the trailing end,
	// rather than at its own.
	var fillsWidth = false
	let select: @MainActor (Value) -> Void

	@State private var isHovered = false

	var body: some View {
		let shape = Squircle(cornerRadius: 11)

		Menu {
			Picker("", selection: Binding(get: { selection }, set: select)) {
				ForEach(options, id: \.1) { option in
					Text(option.0).tag(option.1)
				}
			}
			.pickerStyle(.inline)
			.labelsHidden()
		} label: {
			HStack(spacing: 8) {
				if let icon {
					LucideIcon(icon(selection), size: 14, strokeWidth: 1.75)
						.foregroundStyle(Palette.foreground)
				}

				Text(options.first { $0.1 == selection }?.0 ?? "")
					.font(.system(size: size == .regular ? 14 : 13))
					.foregroundStyle(Palette.foreground)
					.lineLimit(1)

				if fillsWidth {
					Spacer(minLength: 0)
				}

				LucideIcon(chevron == .down ? .chevronDown : .chevronsUpDown, size: chevron == .down ? 16 : 13)
					.foregroundStyle(Palette.mutedForeground)
			}
			.padding(.leading, 12)
			.padding(.trailing, 10)
			.frame(height: size == .regular ? 36 : 32)
			.background(Palette.accent.opacity(isHovered ? 1 : 0), in: shape)
			.overlay {
				shape.strokeBorder(ring, lineWidth: 1)
			}
			.contentShape(shape)
		}
		.menuStyle(.button)
		.buttonStyle(.plain)
		.menuIndicator(.hidden)
		.fixedSize(horizontal: !fillsWidth, vertical: false)
		.onHover { hovering in
			withAnimation(Motion.hover) { isHovered = hovering }
		}
	}
}
