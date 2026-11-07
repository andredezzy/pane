import SwiftUI

// An extension's icon at `size`, or what shows in its place.
struct ExtensionIcon<Fallback: View>: View {
	let icon: NSImage?
	let size: CGFloat
	@ViewBuilder let fallback: Fallback

	var body: some View {
		Group {
			if let icon {
				Image(nsImage: icon)
					.resizable()
					.interpolation(.high)
			} else {
				fallback
			}
		}
		.frame(width: size, height: size)
	}
}

// The first letter of an extension's name on gray.
struct ExtensionLetter: View {
	let name: String
	let fontSize: CGFloat
	let cornerRadius: CGFloat

	var body: some View {
		Text(name.prefix(1).uppercased())
			.font(.system(size: fontSize, weight: .medium))
			.foregroundStyle(.white)
			.frame(maxWidth: .infinity, maxHeight: .infinity)
			.background(Palette.letterTile, in: Squircle(cornerRadius: cornerRadius))
	}
}
