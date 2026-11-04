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
	var labelSize = FormLabel.Size.small
	@ViewBuilder let field: Field

	var body: some View {
		VStack(alignment: .leading, spacing: 0) {
			FormLabel(title: title, note: note, size: labelSize)
				.accessibilityHidden(true)

			field
				.accessibilityLabel(title)
		}
	}
}

// Form labels, medium, 8 above their field, with a note beside them in the
// regular weight.
struct FormLabel: View {
	enum Size {
		// 11 on an 11-point line (Paper's 03.8).
		case small
		// 12 on a 16-point line (the profile sheet's, 05.4 and 05.5).
		case regular
	}

	let title: String
	var note: String?
	var size = Size.small

	var body: some View {
		let fontSize: CGFloat = size == .small ? 11 : 12

		HStack(spacing: 4) {
			Text(title)
				.font(.system(size: fontSize, weight: .medium))
				.foregroundStyle(Palette.foreground)

			if let note {
				Text(note)
					.font(.system(size: fontSize))
					.foregroundStyle(Palette.mutedForeground)
			}
		}
		.lineBox(size == .small ? 11 : 16)
		.padding(.bottom, 8)
	}
}

// A row's title at 13 on 18-point lines and its description at 12 on 16-point
// lines, muted, 2 apart: Settings' rows and the profile sheet's.
struct RowLabels: View {
	let title: String
	var description: String?

	var body: some View {
		VStack(alignment: .leading, spacing: 2) {
			// On 18-point lines (16 of its own), wrapping where the control
			// leaves it too little room (Details' Site access).
			Text(title)
				.font(.system(size: 13))
				.foregroundStyle(Palette.foreground)
				.lineSpacing(2)
				.padding(.vertical, 1)
				.fixedSize(horizontal: false, vertical: true)

			if let description {
				// A status that changes (the update check's, a switch's) crossfades,
				// as the update pill's does.
				Text(description)
					.font(.system(size: 12))
					.lineSpacing(1)
					.padding(.vertical, 0.5)
					.foregroundStyle(Palette.mutedForeground)
					.fixedSize(horizontal: false, vertical: true)
					.contentTransition(.opacity)
					.animation(Motion.status, value: description)
			}
		}
		.frame(maxWidth: .infinity, alignment: .leading)
	}
}
