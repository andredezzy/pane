import SwiftUI

// A CSS line box: single-line text centered in the line height the Electron
// app's CSS gives it (text-xs 16, text-sm 20, text-lg 28, leading-none = the
// font size, and 1.5× a size set without a line height).
extension View {
	func lineBox(_ height: CGFloat) -> some View {
		frame(height: height)
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
