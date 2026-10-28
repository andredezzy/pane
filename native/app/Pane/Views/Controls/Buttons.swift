import SwiftUI

// Pane's buttons: the Electron app's packages/ui Button, with the same
// variants, sizes, and states; the icon buttons; and the sidebar's rows.
// Each owns its hover, its press, and its disabled look, so they behave
// alike: hover fades in 0.15 s, its tint with its fill (DESIGN.md: Motion);
// a press shows at once and fades as it lets go; selection never animates.

enum ButtonVariant {
	case primary
	case destructive
	case outline
	case secondary
	case ghost
}

// A button's height, its padding at either end, and its text.
struct ButtonSize {
	let height: CGFloat
	let leading: CGFloat
	let trailing: CGFloat
	let fontSize: CGFloat

	// h-9 px-4 text-sm
	static let regular = ButtonSize(height: 36, leading: 16, trailing: 16, fontSize: 14)
	// h-8 px-3 text-xs
	static let small = ButtonSize(height: 32, leading: 12, trailing: 12, fontSize: 12)
	// Aside's popups: h-8, 14 in, at 13.
	static let popup = ButtonSize(height: 32, leading: 14, trailing: 14, fontSize: 13)
	// A title and its key's chip (KeyedLabel, which sets its own text): h-9,
	// 14 in, the chip 8 from the end.
	static let keyed = ButtonSize(height: 36, leading: 14, trailing: 8, fontSize: 14)
}

// rounded-md, font-medium; hover changes color, never selection
// (DESIGN.md: Motion); disabled is 50% opacity. Outline has no fill but its
// hover's: the border alone, over whatever it sits on. Another variant takes
// a border when given one (a popup's or a dialog's Cancel).
struct PaneButtonStyle: ButtonStyle {
	var variant: ButtonVariant = .primary
	var size: ButtonSize = .regular
	var fontSize: CGFloat?
	var cornerRadius: CGFloat = 11
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
	@State private var isHovered = false

	private var lit: Bool { isHovered && isEnabled }

	var body: some View {
		let shape = Squircle(cornerRadius: style.cornerRadius)
		// An outline button's 1px border adds to its width (border-box, with
		// the padding inside).
		let outline: CGFloat = style.variant == .outline ? 1 : 0

		configuration.label
			.font(.system(size: style.fontSize ?? style.size.fontSize, weight: .medium))
			.lineLimit(1)
			.foregroundStyle(foreground)
			.padding(.leading, style.size.leading + outline)
			.padding(.trailing, style.size.trailing + outline)
			.frame(maxWidth: style.fillsWidth ? .infinity : nil)
			.frame(height: style.size.height)
			.background {
				ZStack {
					shape.fill(background)
					PressLayer(shape: shape, color: isStrong ? Palette.pressOnStrong : Palette.press, isPressed: configuration.isPressed)
				}
			}
			.overlay {
				if let border = style.border ?? (style.variant == .outline ? Palette.border : nil) {
					shape.strokeBorder(border, lineWidth: 1)
				}
			}
			.contentShape(Rectangle())
			.opacity(isEnabled ? 1 : 0.5)
			.onHover { hovering in
				withAnimation(Motion.hover) { isHovered = hovering }
			}
	}

	// A fill a dark press would not show on.
	private var isStrong: Bool {
		style.fill == nil && [.primary, .destructive].contains(style.variant)
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
			return lit ? Palette.accentForeground : Palette.foreground
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
			return Palette.primary.opacity(lit ? 0.9 : 1)
		case .destructive:
			return Palette.destructive.opacity(lit ? 0.9 : 1)
		case .outline:
			return lit ? Palette.accent : .clear
		case .secondary:
			return Palette.secondary.opacity(lit ? 0.8 : 1)
		case .ghost:
			return lit ? Palette.accent : .clear
		}
	}
}

extension ButtonStyle where Self == PaneButtonStyle {
	static func pane(
		_ variant: ButtonVariant = .primary,
		size: ButtonSize = .regular,
		fontSize: CGFloat? = nil,
		cornerRadius: CGFloat = 11,
		fillsWidth: Bool = false,
		foreground: Color? = nil,
		fill: Color? = nil,
		border: Color? = nil
	) -> PaneButtonStyle {
		PaneButtonStyle(variant: variant, size: size, fontSize: fontSize, cornerRadius: cornerRadius, fillsWidth: fillsWidth, foreground: foreground, fill: fill, border: border)
	}

	// Aside's popups (rounded-lg, h-8, 13 points): the primary, and Cancel
	// on the secondary fill with a hairline.
	static func popup(isPrimary: Bool) -> PaneButtonStyle {
		isPrimary
			? .pane(.primary, size: .popup, cornerRadius: 15)
			: .pane(.secondary, size: .popup, cornerRadius: 15, border: Palette.foreground.opacity(0.08))
	}

	// The dialogs' buttons with their key (KeyedLabel): Cancel on the card
	// with a hairline, Quit in the destructive color, Add to Pane in the
	// primary one. Ghost has no fill at rest: the dialog's card shows.
	static func keyed(_ role: KeyedLabel.Role) -> PaneButtonStyle {
		switch role {
		case .cancel: .pane(.ghost, size: .keyed, cornerRadius: 15, border: Palette.hairlineStrong)
		case .destructive: .pane(.destructive, size: .keyed, cornerRadius: 15)
		case .primary: .pane(.primary, size: .keyed, cornerRadius: 15)
		}
	}
}

// An icon button, as the toolbar's: the icon in a `size`-point square, muted,
// darkening as the shape's fill (the accent, by default) fades in on hover;
// at 40% while disabled, as Aside's gray. `isOn` holds the hover's look while
// what the button opens shows (the extensions menu, an action's popup).
struct IconButtonStyle: ButtonStyle {
	enum Shape {
		case circle
		case squircle(CGFloat)
	}

	var size: CGFloat
	var shape = Shape.circle
	var tint = Palette.mutedForeground
	var hoverTint = Palette.accentForeground
	// No fill: the tint alone shows the hover.
	var fill: Color? = Palette.accent
	var isOn = false

	func makeBody(configuration: Configuration) -> some View {
		IconButton(configuration: configuration, style: self)
	}
}

private struct IconButton: View {
	let configuration: ButtonStyle.Configuration
	let style: IconButtonStyle

	@Environment(\.isEnabled) private var isEnabled
	@State private var isHovered = false

	var body: some View {
		let lit = style.isOn || isHovered && isEnabled
		let shape = switch style.shape {
		case .circle: AnyShape(Circle())
		case let .squircle(radius): AnyShape(Squircle(cornerRadius: radius))
		}

		configuration.label
			.frame(width: style.size, height: style.size)
			.foregroundStyle(lit ? style.hoverTint : style.tint)
			.background {
				ZStack {
					if let fill = style.fill {
						shape.fill(fill).opacity(lit ? 1 : 0)
					}

					PressLayer(shape: shape, color: Palette.press, isPressed: configuration.isPressed)
				}
			}
			.contentShape(shape)
			.opacity(isEnabled ? 1 : 0.4)
			.onHover { hovering in
				withAnimation(Motion.hover) { isHovered = hovering }
			}
	}
}

extension ButtonStyle where Self == IconButtonStyle {
	static func icon(
		size: CGFloat,
		shape: IconButtonStyle.Shape = .circle,
		tint: Color = Palette.mutedForeground,
		hoverTint: Color = Palette.accentForeground,
		fill: Color? = Palette.accent,
		isOn: Bool = false
	) -> IconButtonStyle {
		IconButtonStyle(size: size, shape: shape, tint: tint, hoverTint: hoverTint, fill: fill, isOn: isOn)
	}
}

// A sidebar row: the accent's fill fading in on hover as its text goes from
// muted to the accent's foreground; the selected row's fill (foreground/5)
// and text at once. Its label reads the hover as `isRowHovered`, for what
// shows only then (a tab's close X), which fades with the rest.
struct RowButtonStyle: ButtonStyle {
	var isSelected = false
	var selectedFill = Palette.foreground.opacity(0.05)
	var cornerRadius: CGFloat
	// New keeps its muted text on hover; the others darken.
	var tintsOnHover = true

	func makeBody(configuration: Configuration) -> some View {
		RowButton(configuration: configuration, style: self)
	}
}

private struct RowButton: View {
	let configuration: ButtonStyle.Configuration
	let style: RowButtonStyle

	@State private var isHovered = false

	var body: some View {
		let shape = Squircle(cornerRadius: style.cornerRadius)

		configuration.label
			.foregroundStyle(style.isSelected || style.tintsOnHover && isHovered ? Palette.accentForeground : Palette.mutedForeground)
			.environment(\.isRowHovered, isHovered)
			.background {
				ZStack {
					shape.fill(Palette.accent).opacity(!style.isSelected && isHovered ? 1 : 0)
					shape.fill(style.selectedFill).opacity(style.isSelected ? 1 : 0)
					PressLayer(shape: shape, color: Palette.press, isPressed: configuration.isPressed)
				}
			}
			.onHover { hovering in
				withAnimation(Motion.hover) { isHovered = hovering }
			}
	}
}

extension EnvironmentValues {
	// The pointer is over the row the view is in (RowButtonStyle).
	@Entry var isRowHovered = false
}

// A press over a button's fill. It fades out as the press ends, and only
// it: the click's own effect, a selection, lands at once.
private struct PressLayer<S: Shape>: View {
	let shape: S
	let color: Color
	let isPressed: Bool

	var body: some View {
		shape.fill(color)
			.opacity(isPressed ? 1 : 0)
			.animation(isPressed ? nil : Motion.hover, value: isPressed)
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
