import SwiftUI

// What ⌘Q asks first (Paper: 04.5 Quit Pane, and Quit confirmation in
// 03.5 Overlays): an icon tile, the title and what quitting does, then Cancel
// (Esc) and the destructive Quit (⌘Q again, or Return). The controller takes
// the keys while it shows (BrowserController.confirmQuit).
struct QuitConfirmationView: View {
	let cancel: () -> Void
	let quit: () -> Void

	var body: some View {
		VStack(alignment: .leading, spacing: 16) {
			Image(nsImage: Lucide.circleDashedX.image(strokeWidth: 1.75, size: 22))
				.renderingMode(.template)
				.foregroundStyle(Palette.foreground)
				.frame(width: 40, height: 40)
				.background(Palette.iconTile, in: Squircle(cornerRadius: 18))
				.accessibilityHidden(true)

			DialogHeading(
				title: "Quit Pane?",
				message: "Your profiles and tabs will be here when you open Pane again. Downloads in progress will stop.",
				messageColor: Palette.dialogMessage
			)

			HStack(spacing: 8) {
				Spacer()

				Button(action: cancel) {
					KeyedLabel(title: "Cancel", key: "Esc", role: .cancel)
				}
				.buttonStyle(.keyed(.cancel))

				Button(action: quit) {
					KeyedLabel(title: "Quit", key: "⌘Q", role: .destructive)
				}
				.buttonStyle(.keyed(.destructive))
				.accessibilityIdentifier("pane.quit.confirm")
			}
			.padding(.top, 8)
		}
		.dialogSurface(width: 480, identifier: "pane.quit")
	}
}

// A dialog's title, and what it says under it, in the app's dialogs (Paper: 03.5
// Overlays).
struct DialogHeading: View {
	let title: String
	var message: String?
	var messageColor = Palette.foreground.opacity(0.7)

	var body: some View {
		VStack(alignment: .leading, spacing: 6) {
			Text(title)
				.font(.system(size: 18, weight: .semibold))
				.tracking(-0.18)
				.foregroundStyle(Palette.foreground)
				.lineBox(24)

			if let message {
				Text(message)
					.font(.system(size: 14))
					// text-sm: 20px lines, half the leading above the first
					// and below the last.
					.lineSpacing(3)
					.padding(.vertical, 1.5)
					.foregroundStyle(messageColor)
					.fixedSize(horizontal: false, vertical: true)
			}
		}
	}
}

extension View {
	// A dialog's card, with 24 of padding, for VoiceOver a modal.
	func dialogSurface(width: CGFloat, identifier: String) -> some View {
		padding(24)
			.frame(width: width, alignment: .leading)
			.surfaceBackground(cornerRadius: 29, shadow: Palette.dialogShadow, blur: 64, y: 24)
			.accessibilityElement(children: .contain)
			.accessibilityAddTraits(.isModal)
			.accessibilityIdentifier(identifier)
	}
}

// A button's title and the key that presses it, on a chip: Quit Pane? and the
// extension install dialog, in their buttons' style (`.keyed(role)`).
struct KeyedLabel: View {
	enum Role {
		case cancel
		case destructive
		case primary
	}

	let title: String
	let key: String
	let role: Role

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
		// At its own size: under the button's one-line limit, the chip was
		// offered less than it needs and cut ⌘Q to an ellipsis.
		.fixedSize()
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
		case .cancel: Palette.mutedLifted
		case .destructive: Color.white.opacity(0.85)
		case .primary: Palette.primaryKey
		}
	}

	private var chipColor: Color {
		switch role {
		case .cancel: Palette.secondary
		case .destructive: Color.white.opacity(0.18)
		case .primary: Palette.primaryKeyChip
		}
	}
}
