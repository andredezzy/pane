import SwiftUI

// What the content panel shows with no tab selected, as in the Electron app:
// the Pane mark above one muted line. The mark is drawn in white, so it shows
// on the dark panel only, as the Electron app's does.
struct EmptyStateView: View {
	var body: some View {
		VStack(spacing: 12) {
			PaneMark()
				.stroke(.white, lineWidth: 1.5 * 40 / 24)
				.frame(width: 40, height: 40)
				.accessibilityLabel("Pane")

			Text("Open a profile to start browsing")
				.font(.system(size: 12))
				.foregroundStyle(Palette.mutedForeground)
				.lineBox(16)
		}
		.frame(maxWidth: .infinity, maxHeight: .infinity)
		.background(Palette.card)
		.accessibilityElement(children: .contain)
		.accessibilityIdentifier("pane.empty")
	}
}

// A rounded square split down the middle (the 24-unit icon: an 18-unit
// square at 3, radius 4, and the line at 12).
private struct PaneMark: Shape {
	func path(in rect: CGRect) -> Path {
		let unit = rect.width / 24
		var path = Path(roundedRect: CGRect(x: 3 * unit, y: 3 * unit, width: 18 * unit, height: 18 * unit), cornerRadius: 4 * unit)
		path.move(to: CGPoint(x: 12 * unit, y: 3 * unit))
		path.addLine(to: CGPoint(x: 12 * unit, y: 21 * unit))

		return path
	}
}
