import SwiftUI

enum PaneMenuSize {
	// Beside the small buttons (h-8, at 13): Settings', the Extensions page's.
	case small
	// A segment of a field, at 13: as tall as the field, whose border and
	// corners it takes (the profile sheet's proxy address).
	case embedded
}

enum PaneMenuChevron {
	// A select's single chevron down, 16 points.
	case down
	// Aside's pop-up button's up and down, 13 points.
	case upDown
}

// A menu's trigger, as Aside's pop-up button and packages/ui's Select: the
// value (with its icon, if it has one) and a chevron, in a 1-point border,
// with no fill but the foreground at 4% on hover (ADR 0063). A click shows the
// system's menu of `content`.
struct PaneMenuTrigger<Content: View>: View {
	let title: String
	// Embedded, the titles whose longest it holds the room of, so what follows
	// it in the field stays where it is when the choice changes.
	var titles: [String] = []
	// What it sets, for VoiceOver, which reads the title as its value.
	var label: String?
	var size = PaneMenuSize.small
	var icon: Lucide?
	var chevron = PaneMenuChevron.down
	// The Extensions page's takes the profile's color.
	var ring = Palette.border
	@ViewBuilder let content: Content

	@State private var isHovered = false

	var body: some View {
		let isEmbedded = size == .embedded
		let shape = Squircle(cornerRadius: isEmbedded ? 0 : 11)

		Menu {
			content
		} label: {
			HStack(spacing: isEmbedded ? 6 : 8) {
				if let icon {
					LucideIcon(icon, size: 14, strokeWidth: 1.75)
						.foregroundStyle(Palette.foreground)
				}

				titleText

				LucideIcon(chevron == .down ? .chevronDown : .chevronsUpDown, size: isEmbedded ? 12 : chevron == .down ? 16 : 13)
					.foregroundStyle(isEmbedded ? Palette.mutedLifted : Palette.mutedForeground)
			}
			// 12 and 10 inside the 1-point border, as CSS's padding beside it;
			// embedded, the field's border is outside them.
			.padding(.leading, isEmbedded ? 12 : 13)
			.padding(.trailing, isEmbedded ? 10 : 11)
			.frame(height: isEmbedded ? nil : 32)
			.frame(maxHeight: isEmbedded ? .infinity : nil)
			.background(Palette.outlineHover.opacity(isHovered ? 1 : 0), in: shape)
			.overlay {
				if !isEmbedded {
					shape.strokeBorder(ring, lineWidth: 1)
				}
			}
			.contentShape(shape)
		}
		.menuStyle(.button)
		.buttonStyle(.plain)
		.menuIndicator(.hidden)
		.fixedSize(horizontal: true, vertical: false)
		.onHover { hovering in
			withAnimation(Motion.hover) { isHovered = hovering }
		}
		.accessibilityLabel(label ?? title)
		.accessibilityValue(label == nil ? "" : title)
	}

	private var titleText: some View {
		ZStack(alignment: .leading) {
			ForEach(titles, id: \.self) { title in
				Text(title)
					.hidden()
			}

			Text(title)
				.foregroundStyle(Palette.foreground)
		}
		.font(.system(size: 13))
		.lineLimit(1)
	}
}

// The app's one menu of a flat list of options, the current one checked.
struct PaneMenuButton<Value: Hashable>: View {
	let options: [(String, Value)]
	let selection: Value
	var label: String?
	var size = PaneMenuSize.small
	var icon: ((Value) -> Lucide)?
	var chevron = PaneMenuChevron.down
	var ring = Palette.border
	let select: @MainActor (Value) -> Void

	var body: some View {
		PaneMenuTrigger(
			title: options.first { $0.1 == selection }?.0 ?? "",
			titles: size == .embedded ? options.map(\.0) : [],
			label: label,
			size: size,
			icon: icon?(selection),
			chevron: chevron,
			ring: ring
		) {
			Picker("", selection: Binding(get: { selection }, set: select)) {
				ForEach(options, id: \.1) { option in
					Text(option.0).tag(option.1)
				}
			}
			.pickerStyle(.inline)
			.labelsHidden()
		}
	}
}
