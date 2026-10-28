import SwiftUI

// A 1px line in the border color.
struct PaneSeparator: View {
	var body: some View {
		Rectangle()
			.fill(Palette.border)
			.frame(height: 1)
	}
}
