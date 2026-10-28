import SwiftUI

// "Press Esc to exit full screen": shown at the top for a few seconds when a
// page takes fullscreen or locks the pointer, then fades.
struct ExclusiveAccessHint: View {
	let text: String

	@State private var shown = true

	var body: some View {
		Text(text)
			.font(.system(size: 13, weight: .medium))
			.foregroundStyle(.white)
			.padding(.horizontal, 16)
			.padding(.vertical, 9)
			.background(.black.opacity(0.75), in: Capsule(style: .circular))
			.padding(.top, 24)
			.opacity(shown ? 1 : 0)
			.allowsHitTesting(false)
			.accessibilityIdentifier("pane.exclusiveAccessHint")
			.task(id: text) {
				shown = true
				try? await Task.sleep(for: .seconds(3.5))
				withAnimation(Motion.fade) {
					shown = false
				}
			}
	}
}
