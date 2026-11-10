import PaneKit
import SwiftUI

// What the update pill shared with the lock screen, which still draws them
// (LockScreenView, until L1 ports it): the pill itself is the Views line's
// (pane/ui/sidebar/update_pill.cc, H4).

// The 6px emerald dot; it pulses while the dmg downloads.
struct UpdateDot: View {
	let pulsing: Bool

	@State private var dimmed = false

	var body: some View {
		Circle()
			.fill(Palette.success)
			.frame(width: 6, height: 6)
			.opacity(pulsing && dimmed ? 0.5 : 1)
			.onAppear { pulse() }
			.onChange(of: pulsing) { pulse() }
	}

	// Tailwind's animate-pulse: half opacity and back, every two seconds.
	private func pulse() {
		guard pulsing, let pulse = Motion.unlessReduced(.easeInOut(duration: 1).repeatForever(autoreverses: true)) else {
			dimmed = false
			return
		}

		withAnimation(pulse) {
			dimmed = true
		}
	}
}

// A 4px track at 10% with the emerald share downloaded.
struct UpdateProgressBar: View {
	let progress: Double

	var body: some View {
		Capsule(style: .circular)
			.fill(Palette.foreground.opacity(0.1))
			.overlay {
				// A shape over the whole bar, so its animation is of the share alone:
				// a frame's would carry the bar's own position too, and left the fill
				// behind as the lock's panel slid up with it.
				ProgressFill(share: min(max(progress, 0), 1))
					.fill(Palette.success)
					.animation(Motion.progress, value: progress)
			}
			.frame(height: 4)
	}
}

private struct ProgressFill: Shape {
	var share: Double

	var animatableData: Double {
		get { share }
		set { share = newValue }
	}

	func path(in rect: CGRect) -> Path {
		Capsule(style: .circular).path(in: CGRect(x: rect.minX, y: rect.minY, width: rect.width * share, height: rect.height))
	}
}
