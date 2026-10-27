import AppKit
import SwiftUI

// The Electron app's packages/ui controls: Button, Input, Switch, Separator,
// and the settings labels, with the same variants, sizes, and states.

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

// Input: h-9 rounded-md, a 1px input border, px-3 text-sm, muted
// placeholder; focus draws a 1px ring.
struct PaneTextField: View {
	let placeholder: String
	@Binding var text: String
	var monospaced = false
	// type="password": the text shows as dots.
	var secure = false
	// autoFocus: the field takes focus as it appears.
	var focusesOnAppear = false
	var onSubmit: () -> Void = {}

	@FocusState private var focused: Bool
	@Environment(\.isEnabled) private var isEnabled

	var body: some View {
		field
		.textFieldStyle(.plain)
		.font(monospaced ? .system(size: 11, design: .monospaced) : .system(size: 14))
		.foregroundStyle(Palette.foreground)
		.focused($focused)
		.onSubmit(onSubmit)
		.padding(.horizontal, 12)
		.frame(height: 36)
		.background(Palette.background, in: Squircle(cornerRadius: 11))
		.overlay {
			Squircle(cornerRadius: 11)
				.strokeBorder(focused ? Palette.ring : Palette.border, lineWidth: 1)
		}
		.opacity(isEnabled ? 1 : 0.5)
		.onAppear {
			if focusesOnAppear {
				// Focus set during onAppear is dropped: the field is not in
				// the window yet. One turn of the main loop later it is.
				DispatchQueue.main.async { focused = true }
			}
		}
	}
}

extension PaneTextField {
	@ViewBuilder
	fileprivate var field: some View {
		let prompt = Text(placeholder).foregroundStyle(Palette.mutedForeground)

		if secure {
			SecureField("", text: $text, prompt: prompt)
		} else {
			TextField("", text: $text, prompt: prompt)
		}
	}
}

// Select (packages/ui select.tsx): an input-like trigger, h-9 rounded-md
// with a 1px border, the value in text-sm and a chevron at 50%; its menu is
// the system's.
struct PaneSelect<Option: Hashable>: View {
	let options: [Option]
	@Binding var selection: Option
	let title: (Option) -> String

	var body: some View {
		Menu {
			ForEach(options, id: \.self) { option in
				Button(title(option)) { selection = option }
			}
		} label: {
			HStack(spacing: 8) {
				Text(title(selection))
					.font(.system(size: 14))
					.foregroundStyle(Palette.foreground)
				Spacer(minLength: 0)
				LucideIcon(.chevronDown, size: 16)
					.foregroundStyle(Palette.foreground.opacity(0.5))
			}
			.padding(.horizontal, 12)
			.frame(height: 36)
			.contentShape(Rectangle())
		}
		.menuStyle(.button)
		.buttonStyle(.plain)
		.menuIndicator(.hidden)
		.overlay {
			Squircle(cornerRadius: 11)
				.strokeBorder(Palette.border, lineWidth: 1)
		}
	}
}

// Switch: h-5 w-9, primary when on, the input color when off, a 16px thumb.
struct PaneSwitchStyle: ToggleStyle {
	func makeBody(configuration: Configuration) -> some View {
		PaneSwitch(configuration: configuration)
	}
}

private struct PaneSwitch: View {
	let configuration: ToggleStyle.Configuration

	@Environment(\.isEnabled) private var isEnabled
	@Environment(\.accessibilityReduceMotion) private var reduceMotion

	var body: some View {
		HStack(spacing: 8) {
			configuration.label

			Capsule(style: .circular)
				.fill(configuration.isOn ? Palette.primary : Palette.border)
				.frame(width: 36, height: 20)
				.overlay(alignment: configuration.isOn ? .trailing : .leading) {
					Circle()
						.fill(Palette.background)
						.frame(width: 16, height: 16)
						.shadow(color: .black.opacity(0.15), radius: 4, y: 2)
						.padding(2)
				}
				.contentShape(Capsule(style: .circular))
				.onTapGesture {
					withAnimation(reduceMotion ? nil : .easeOut(duration: 0.15)) {
						configuration.isOn.toggle()
					}
				}
		}
		.opacity(isEnabled ? 1 : 0.5)
	}
}

extension ToggleStyle where Self == PaneSwitchStyle {
	static var paneSwitch: PaneSwitchStyle { PaneSwitchStyle() }
}

// A 1px line in the border color.
struct PaneSeparator: View {
	var body: some View {
		Rectangle()
			.fill(Palette.border)
			.frame(height: 1)
	}
}

// Settings section labels: 10px medium caps, wide tracking, muted.
struct SectionLabel: View {
	let title: String

	var body: some View {
		// An inline span in a 16px block: a 24px line box, the 10px text on
		// the line's baseline, a little below its middle.
		Text(title.uppercased())
			.font(.system(size: 10, weight: .medium))
			.tracking(0.25)
			.foregroundStyle(Palette.mutedForeground)
			.offset(y: 2.5)
			.lineBox(24)
	}
}

// Form labels: 11px medium. The Electron app's label is inline in a 16px
// block, so it takes a 24px line with its text 7.5px down, and the field
// follows it directly (the label's margin does not apply inline).
struct FormLabel: View {
	let title: String
	var note: String?

	var body: some View {
		HStack(spacing: 4) {
			Text(title)
				.foregroundStyle(Palette.foreground)

			if let note {
				Text(note)
					.foregroundStyle(Palette.mutedForeground)
			}
		}
		.font(.system(size: 11, weight: .medium))
		.frame(height: 13)
		.padding(.top, 7.5)
		.frame(height: 24, alignment: .top)
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
