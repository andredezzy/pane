import SwiftUI

// A 1-point line across, in the border color unless given another: a
// separator, a menu's, a settings row's, a sheet's header and footer rules.
struct Hairline: View {
	var color = Palette.border

	var body: some View {
		Rectangle()
			.fill(color)
			.frame(height: 1)
	}
}
