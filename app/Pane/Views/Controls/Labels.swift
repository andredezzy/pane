import SwiftUI

// A CSS line box: single-line text centered in the line height the Electron
// app's CSS gives it (text-xs 16, text-sm 20, text-lg 28, leading-none = the
// font size, and 1.5× a size set without a line height).
extension View {
	func lineBox(_ height: CGFloat) -> some View {
		frame(height: height)
	}
}
