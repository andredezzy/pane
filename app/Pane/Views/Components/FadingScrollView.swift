import SwiftUI

// A vertical scroll view whose edges fade as it scrolls: the top fade grows
// from nothing to its full height over the first points scrolled, the bottom
// one shrinks to nothing over the last, so nothing fades at either end or when
// the content fits. The sidebar's list and the profile sheet's body.
struct FadingScrollView<Content: View>: View {
	@ViewBuilder let content: Content

	@State private var top: CGFloat = 0
	@State private var bottom: CGFloat = 0

	private static var space: String { "FadingScrollView" }

	var body: some View {
		GeometryReader { viewport in
			ScrollView {
				content
					.background {
						GeometryReader { geometry in
							Color.clear.preference(key: ScrollMetricsKey.self, value: ScrollMetrics(content: geometry.frame(in: .named(Self.space)), viewportHeight: viewport.size.height))
						}
					}
			}
			.coordinateSpace(name: Self.space)
			.onPreferenceChange(ScrollMetricsKey.self) { metrics in
				let scrolled = -metrics.content.minY
				let newTop = min(max(scrolled, 0), ScrollFade.height)
				let newBottom = min(max(metrics.content.height - metrics.viewportHeight - scrolled, 0), ScrollFade.height)

				// Only a change of either fade draws the mask again.
				if newTop != top {
					top = newTop
				}

				if newBottom != bottom {
					bottom = newBottom
				}
			}
			.mask {
				VStack(spacing: 0) {
					LinearGradient(stops: ScrollFade.stops, startPoint: .top, endPoint: .bottom)
						.frame(height: top)
					Color.black
					LinearGradient(stops: ScrollFade.stops, startPoint: .bottom, endPoint: .top)
						.frame(height: bottom)
				}
			}
		}
	}
}

// The content's frame in the scroll view, and the viewport's height, which a
// resize changes with the content's frame unmoved: a window shrunk under a list
// that fit has to fade too.
private struct ScrollMetrics: Equatable {
	var content = CGRect.zero
	var viewportHeight: CGFloat = 0
}

private struct ScrollMetricsKey: PreferenceKey {
	static let defaultValue = ScrollMetrics()

	// Whichever sibling reports them: the others report none.
	static func reduce(value: inout ScrollMetrics, nextValue: () -> ScrollMetrics) {
		let next = nextValue()

		if next != defaultValue {
			value = next
		}
	}
}

// How far a fading scroll view's edges fade, 40 points, and how: the content's
// alpha is the square of its share of the way in from the edge, so it stays
// faint across most of the fade and only then comes up (ADR 0061).
enum ScrollFade {
	static let height: CGFloat = 40

	static let stops = stride(from: 0.0, through: 1.0, by: 0.25).map { share in
		Gradient.Stop(color: .black.opacity(share * share), location: share)
	}
}
