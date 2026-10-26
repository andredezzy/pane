import AppKit
import PaneKit
import SwiftUI

extension ProfileColor {
	var color: Color {
		Color(hex: hex)
	}
}

extension Color {
	// "#rrggbb"
	init(hex: String) {
		let value = UInt64(hex.dropFirst(), radix: 16) ?? 0
		self.init(
			red: Double((value >> 16) & 0xff) / 255,
			green: Double((value >> 8) & 0xff) / 255,
			blue: Double(value & 0xff) / 255
		)
	}
}

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
					RoundedRectangle(cornerRadius: cornerRadius, style: .circular)
						.fill(hoverFill)
						.opacity(!isSelected && isHovered ? 1 : 0)
						.animation(reduceMotion ? nil : .easeOut(duration: 0.15), value: isHovered)
					RoundedRectangle(cornerRadius: cornerRadius, style: .circular)
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

// A favicon from the data: URL tabs store (12px, rounded-sm), or a globe.
struct FaviconView: View {
	let dataURL: String

	var body: some View {
		if let image = Self.image(from: dataURL) {
			Image(nsImage: image)
				.resizable()
				.interpolation(.high)
				.frame(width: 12, height: 12)
				.clipShape(RoundedRectangle(cornerRadius: 2, style: .circular))
		} else {
			LucideIcon(.globe, size: 12)
		}
	}

	private static var cache: [String: NSImage] = [:]

	static func image(from dataURL: String) -> NSImage? {
		guard !dataURL.isEmpty else {
			return nil
		}

		if let image = cache[dataURL] {
			return image
		}

		guard
			let comma = dataURL.firstIndex(of: ","),
			let data = Data(base64Encoded: String(dataURL[dataURL.index(after: comma)...])),
			let image = NSImage(data: data)
		else {
			return nil
		}

		cache[dataURL] = image

		return image
	}
}
