import SwiftUI

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
