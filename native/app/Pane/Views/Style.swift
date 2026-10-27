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

// A favicon from the data: URL tabs store (16 points, as Aside's), or a globe.
struct FaviconView: View {
	let dataURL: String

	var body: some View {
		if let image = Self.image(from: dataURL) {
			Image(nsImage: image)
				.resizable()
				.interpolation(.high)
				.frame(width: 16, height: 16)
				.clipShape(Squircle(cornerRadius: 4))
		} else {
			LucideIcon(.globe, size: 16)
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

// A vertical scroll view whose edges fade as it scrolls, as the Electron
// app's scroll-fade-y: the top fade grows from 0 to 24 points over the first
// 24 scrolled, the bottom one shrinks to 0 over the last 24, so nothing fades
// at either end or when the content fits.
struct FadingScrollView<Content: View>: View {
	@ViewBuilder let content: Content

	@State private var top: CGFloat = 0
	@State private var bottom: CGFloat = 0

	private static var fade: CGFloat { 24 }
	private static var space: String { "FadingScrollView" }

	var body: some View {
		GeometryReader { viewport in
			ScrollView {
				content
					.background {
						GeometryReader { geometry in
							Color.clear.preference(key: ContentFrameKey.self, value: geometry.frame(in: .named(Self.space)))
						}
					}
			}
			.coordinateSpace(name: Self.space)
			.onPreferenceChange(ContentFrameKey.self) { frame in
				let scrolled = -frame.minY
				let newTop = min(max(scrolled, 0), Self.fade)
				let newBottom = min(max(frame.height - viewport.size.height - scrolled, 0), Self.fade)

				// Only a change of either fade draws the mask again: a resize
				// that scrolls nothing leaves both.
				if newTop != top {
					top = newTop
				}

				if newBottom != bottom {
					bottom = newBottom
				}
			}
			.mask {
				VStack(spacing: 0) {
					LinearGradient(colors: [.clear, .black], startPoint: .top, endPoint: .bottom)
						.frame(height: top)
					Color.black
					LinearGradient(colors: [.black, .clear], startPoint: .top, endPoint: .bottom)
						.frame(height: bottom)
				}
			}
		}
	}
}

private struct ContentFrameKey: PreferenceKey {
	static let defaultValue = CGRect.zero

	// The content's frame, whichever sibling reports it: the others report
	// none.
	static func reduce(value: inout CGRect, nextValue: () -> CGRect) {
		let next = nextValue()

		if next != .zero {
			value = next
		}
	}
}
