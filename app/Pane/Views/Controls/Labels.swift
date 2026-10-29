import SwiftUI

// A CSS line box: single-line text centered in the line height the Electron
// app's CSS gives it (text-xs 16, text-sm 20, text-lg 28, leading-none = the
// font size, and 1.5× a size set without a line height).
extension View {
	func lineBox(_ height: CGFloat) -> some View {
		frame(height: height)
	}
}

// The Extensions page's list label: 10px medium caps, tracked 0.05em, muted,
// on a 15px line (Paper's 09.1).
struct SectionLabel: View {
	let title: String

	var body: some View {
		Text(title.uppercased())
			.font(.system(size: 10, weight: .medium))
			.tracking(0.5)
			.foregroundStyle(Palette.mutedForeground)
			.lineBox(15)
	}
}

// A form's field under its label, which names it for VoiceOver: the label is
// drawn beside the field, not tied to it, so the field read only its
// placeholder ("e.g. Work account").
struct LabeledField<Field: View>: View {
	let title: String
	var note: String?
	@ViewBuilder let field: Field

	var body: some View {
		VStack(alignment: .leading, spacing: 0) {
			FormLabel(title: title, note: note)
				.accessibilityHidden(true)

			field
				.accessibilityLabel(title)
		}
	}
}

// Form labels: 11px medium on an 11px line, 8 above their field (Paper's
// 03.8 and 05 boards).
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
		.lineBox(11)
		.padding(.bottom, 8)
	}
}
