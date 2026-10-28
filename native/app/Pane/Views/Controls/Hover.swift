import SwiftUI

// Hover feedback animates; selection never does (DESIGN.md: Motion).
struct HoverBackground: ViewModifier {
	let isSelected: Bool
	let selectedFill: Color
	let hoverFill: Color
	let cornerRadius: CGFloat

	@State private var isHovered = false
	@Environment(\.accessibilityReduceMotion) private var reduceMotion

	func body(content: Content) -> some View {
		content
			.background {
				ZStack {
					Squircle(cornerRadius: cornerRadius)
						.fill(hoverFill)
						.opacity(!isSelected && isHovered ? 1 : 0)
						.animation(reduceMotion ? nil : .easeOut(duration: 0.15), value: isHovered)
					Squircle(cornerRadius: cornerRadius)
						.fill(selectedFill)
						.opacity(isSelected ? 1 : 0)
				}
			}
			.onHover { isHovered = $0 }
	}
}

extension View {
	// Selected rows are foreground/5; hover is the accent color.
	func hoverBackground(isSelected: Bool, selectedFill: Color = Palette.foreground.opacity(0.05), hoverFill: Color = Palette.accent, cornerRadius: CGFloat = 5) -> some View {
		modifier(HoverBackground(isSelected: isSelected, selectedFill: selectedFill, hoverFill: hoverFill, cornerRadius: cornerRadius))
	}
}
