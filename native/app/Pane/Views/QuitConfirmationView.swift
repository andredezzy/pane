import SwiftUI

// What ⌘Q asks first (Paper: 03.5 Quit Pane, and Quit confirmation in
// 02.5 Overlays): an icon tile, the title and what quitting does, then Cancel
// (Esc) and the destructive Quit (⌘Q again, or Return). The controller takes
// the keys while it shows (BrowserController.confirmQuit).
struct QuitConfirmationView: View {
	let cancel: () -> Void
	let quit: () -> Void

	// muted at 70% on light; the dark muted on dark.
	private static let tile = Color(nsColor: NSColor(name: nil) { appearance in
		Palette.isDark(appearance) ? NSColor(rgb: 0x232326) : NSColor(rgb: 0xE9E9EB).withAlphaComponent(0.7)
	})

	// The dialogs' shadow: this one's and the extension install dialog's.
	static let shadow = Color(nsColor: NSColor(name: nil) { appearance in
		NSColor(white: 0, alpha: Palette.isDark(appearance) ? 0.5 : 0.16)
	})

	var body: some View {
		VStack(alignment: .leading, spacing: 16) {
			Image(nsImage: Lucide.circleDashedX.image(strokeWidth: 1.75, size: 22))
				.renderingMode(.template)
				.foregroundStyle(Palette.foreground)
				.frame(width: 40, height: 40)
				.background(Self.tile, in: Squircle(cornerRadius: 18))
				.accessibilityHidden(true)

			VStack(alignment: .leading, spacing: 6) {
				Text("Quit Pane?")
					.font(.system(size: 18, weight: .semibold))
					.tracking(-0.18)
					.foregroundStyle(Palette.foreground)

				Text("Your profiles and tabs will be here when you open Pane again. Downloads in progress will stop.")
					.font(.system(size: 14))
					// text-sm: 20px lines.
					.lineSpacing(3)
					.foregroundStyle(Palette.foreground.opacity(0.7))
					.fixedSize(horizontal: false, vertical: true)
			}

			HStack(spacing: 8) {
				Spacer()

				Button(action: cancel) {
					KeyedLabel(title: "Cancel", key: "Esc", role: .cancel)
				}
				.buttonStyle(KeyedButtonStyle(role: .cancel))

				Button(action: quit) {
					KeyedLabel(title: "Quit", key: "⌘Q", role: .destructive)
				}
				.buttonStyle(KeyedButtonStyle(role: .destructive))
				.accessibilityIdentifier("pane.quit.confirm")
			}
			.padding(.top, 8)
		}
		.padding(24)
		.frame(width: 480, alignment: .leading)
		.surfaceBackground(cornerRadius: 29, shadow: Self.shadow, blur: 64, y: 24)
		.accessibilityElement(children: .contain)
		.accessibilityAddTraits(.isModal)
		.accessibilityIdentifier("pane.quit")
	}
}

// A button's title and the key that presses it, on a chip: Quit Pane? and the
// extension install dialog.
struct KeyedLabel: View {
	let title: String
	let key: String
	let role: KeyedButtonStyle.Role

	private static let cancelKey = Color(nsColor: NSColor(name: nil) { appearance in
		NSColor(rgb: Palette.isDark(appearance) ? 0xA1A1AA : 0x71717A)
	})

	var body: some View {
		HStack(spacing: 8) {
			Text(title)
				.font(.system(size: 14, weight: .medium))
				.foregroundStyle(titleColor)

			Text(key)
				.font(.system(size: 12, weight: .medium))
				.foregroundStyle(keyColor)
				.padding(.horizontal, 6)
				.frame(height: 20)
				.background(chipColor, in: Squircle(cornerRadius: 9))
				.accessibilityHidden(true)
		}
	}

	private var titleColor: Color {
		switch role {
		case .cancel: Palette.foreground
		case .destructive: Palette.destructiveForeground
		case .primary: Palette.primaryForeground
		}
	}

	// On a filled button, the key's chip is the button's text color, faint.
	private var keyColor: Color {
		switch role {
		case .cancel: Self.cancelKey
		case .destructive: Color.white.opacity(0.85)
		case .primary: Palette.primaryForeground.opacity(0.85)
		}
	}

	private var chipColor: Color {
		switch role {
		case .cancel: Palette.secondary
		case .destructive: Color.white.opacity(0.18)
		case .primary: Palette.primaryForeground.opacity(0.18)
		}
	}
}

// h-9 rounded-lg, the key on a chip at its trailing end: Cancel on the card
// with a hairline, Quit in the destructive color, Add to Pane in the primary
// one. Hover changes color only.
struct KeyedButtonStyle: ButtonStyle {
	enum Role {
		case cancel
		case destructive
		case primary
	}

	let role: Role

	func makeBody(configuration: Configuration) -> some View {
		KeyedButton(configuration: configuration, role: role)
	}
}

private struct KeyedButton: View {
	let configuration: ButtonStyle.Configuration
	let role: KeyedButtonStyle.Role

	@State private var isHovered = false

	// The cancel button's hairline: the border token, a little stronger on
	// dark.
	private static let hairline = Color(nsColor: NSColor(name: nil) { appearance in
		Palette.isDark(appearance) ? NSColor(white: 1, alpha: 0.1) : NSColor(white: 0, alpha: 0.08)
	})

	var body: some View {
		configuration.label
			.padding(.leading, 14)
			.padding(.trailing, 8)
			.frame(height: 36)
			.background(fill, in: Squircle(cornerRadius: 15))
			.overlay {
				if role == .cancel {
					Squircle(cornerRadius: 15)
						.strokeBorder(Self.hairline, lineWidth: 1)
				}
			}
			.contentShape(Rectangle())
			.onHover { hovering in
				withAnimation(Motion.hover) {
					isHovered = hovering
				}
			}
	}

	private var fill: Color {
		switch role {
		case .cancel:
			isHovered ? Palette.accent : Palette.card
		case .destructive:
			Palette.destructive.opacity(isHovered ? 0.9 : 1)
		case .primary:
			Palette.primary.opacity(isHovered ? 0.9 : 1)
		}
	}
}
