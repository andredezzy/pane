import SwiftUI

// "Press Esc to exit full screen": shown at the top for a few seconds when a
// page takes fullscreen or locks the pointer, then fades.
struct ExclusiveAccessHint: View {
	let text: String

	@State private var shown = true
	@Environment(\.accessibilityReduceMotion) private var reduceMotion

	var body: some View {
		Text(text)
			.font(.system(size: 13, weight: .medium))
			.foregroundStyle(.white)
			.padding(.horizontal, 16)
			.padding(.vertical, 9)
			.background(.black.opacity(0.75), in: Capsule())
			.padding(.top, 24)
			.opacity(shown ? 1 : 0)
			.allowsHitTesting(false)
			.accessibilityIdentifier("pane.exclusiveAccessHint")
			.task(id: text) {
				shown = true
				try? await Task.sleep(for: .seconds(3.5))
				withAnimation(reduceMotion ? nil : .easeOut(duration: 0.3)) {
					shown = false
				}
			}
	}
}
