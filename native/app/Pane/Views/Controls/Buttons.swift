import SwiftUI

// The Electron app's packages/ui Button, with the same variants, sizes, and
// states (its Input, Select, Switch, Separator, and labels are this folder's
// other files).

enum ButtonVariant {
	case primary
	case destructive
	case outline
	case secondary
	case ghost
}

enum ButtonSize {
	// h-9 px-4 text-sm
	case regular
	// h-8 px-3 text-xs
	case small

	var height: CGFloat { self == .regular ? 36 : 32 }
	var padding: CGFloat { self == .regular ? 16 : 12 }
	var fontSize: CGFloat { self == .regular ? 14 : 12 }
}

// rounded-md, font-medium; hover changes color, never selection
// (DESIGN.md: Motion); disabled is 50% opacity. Outline has no fill but its
// hover's: the border alone, over whatever it sits on.
struct PaneButtonStyle: ButtonStyle {
	var variant: ButtonVariant = .primary
	var size: ButtonSize = .regular
	var fontSize: CGFloat?
	var fillsWidth = false
	var foreground: Color?
	// A state's own colors over the variant's (className overrides).
	var fill: Color?
	var border: Color?

	func makeBody(configuration: Configuration) -> some View {
		PaneButtonBody(configuration: configuration, style: self)
	}
}

private struct PaneButtonBody: View {
	let configuration: ButtonStyle.Configuration
	let style: PaneButtonStyle

	@Environment(\.isEnabled) private var isEnabled
	@Environment(\.accessibilityReduceMotion) private var reduceMotion
	@State private var isHovered = false

	var body: some View {
		configuration.label
			.font(.system(size: style.fontSize ?? style.size.fontSize, weight: .medium))
			.lineLimit(1)
			.foregroundStyle(foreground)
			// An outline button's 1px border adds to its width (border-box,
			// with the padding inside).
			.padding(.horizontal, style.size.padding + (style.variant == .outline ? 1 : 0))
			.frame(maxWidth: style.fillsWidth ? .infinity : nil)
			.frame(height: style.size.height)
			.background(background, in: Squircle(cornerRadius: 11))
			.overlay {
				if style.variant == .outline {
					Squircle(cornerRadius: 11)
						.strokeBorder(style.border ?? Palette.border, lineWidth: 1)
				}
			}
			.contentShape(Rectangle())
			.opacity(isEnabled ? 1 : 0.5)
			.onHover { hovering in
				withAnimation(reduceMotion ? nil : .easeOut(duration: 0.15)) {
					isHovered = hovering && isEnabled
				}
			}
	}

	private var foreground: Color {
		if let color = style.foreground {
			return color
		}

		switch style.variant {
		case .primary:
			return Palette.primaryForeground
		case .destructive:
			return Palette.destructiveForeground
		case .outline, .ghost:
			return isHovered ? Palette.accentForeground : Palette.foreground
		case .secondary:
			return Palette.foreground
		}
	}

	private var background: Color {
		if let fill = style.fill {
			return fill
		}

		switch style.variant {
		case .primary:
			return Palette.primary.opacity(isHovered ? 0.9 : 1)
		case .destructive:
			return Palette.destructive.opacity(isHovered ? 0.9 : 1)
		case .outline:
			return isHovered ? Palette.accent : .clear
		case .secondary:
			return Palette.secondary.opacity(isHovered ? 0.8 : 1)
		case .ghost:
			return isHovered ? Palette.accent : .clear
		}
	}
}

extension ButtonStyle where Self == PaneButtonStyle {
	static func pane(
		_ variant: ButtonVariant = .primary,
		size: ButtonSize = .regular,
		fontSize: CGFloat? = nil,
		fillsWidth: Bool = false,
		foreground: Color? = nil,
		fill: Color? = nil,
		border: Color? = nil
	) -> PaneButtonStyle {
		PaneButtonStyle(variant: variant, size: size, fontSize: fontSize, fillsWidth: fillsWidth, foreground: foreground, fill: fill, border: border)
	}
}

// A row of equal buttons as CSS flex-1 lays them out: the width shared before
// borders, so outline buttons end 2px wider than filled ones.
struct PresetRow<Option: Hashable>: View {
	let options: [Option]
	let selection: Option
	let title: (Option) -> String
	let select: (Option) -> Void

	var body: some View {
		GeometryReader { geometry in
			let borders = CGFloat(options.filter { $0 != selection }.count) * 2
			let share = (geometry.size.width - CGFloat(options.count - 1) * 4 - borders) / CGFloat(options.count)

			HStack(spacing: 4) {
				ForEach(options, id: \.self) { option in
					let isSelected = option == selection

					Button(title(option)) { select(option) }
						.buttonStyle(.pane(isSelected ? .primary : .outline, size: .small, fontSize: 11, fillsWidth: true))
						.frame(width: share + (isSelected ? 0 : 2))
						.accessibilityAddTraits(isSelected ? .isSelected : [])
				}
			}
		}
		.frame(height: 32)
	}
}
