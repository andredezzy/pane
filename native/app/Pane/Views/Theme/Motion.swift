import AppKit
import SwiftUI

// Every animation in Pane, by what it is for (DESIGN.md › Motion), each nil
// while the Mac reduces motion: a view writes `.animation(Motion.hover,
// value:)` or `withAnimation(Motion.hover)` and honors the setting with no
// check of its own. Each is read as its change happens, so a change of the
// setting applies from the next one.
enum Motion {
	// Hover fills, the tints that change with them, and presses: 0.15 s,
	// easing out.
	static var hover: Animation? { unlessReduced(.easeOut(duration: 0.15)) }

	// A switch's thumb crossing over, as quick as a hover.
	static var toggle: Animation? { unlessReduced(.easeOut(duration: 0.15)) }

	// An indicator's own change of shape (the active profile's bar growing
	// into its square): short and settled, about a third of a second, with
	// no overshoot to speak of.
	static var morph: Animation? { unlessReduced(.spring(response: 0.32, dampingFraction: 0.86)) }

	// Rows sliding aside for a dragged one, and its copy settling into place.
	static var slide: Animation? { unlessReduced(.spring(response: 0.28, dampingFraction: 0.9)) }

	// The copy of a dragged row going, once it has settled.
	static var drop: Animation? { unlessReduced(.easeOut(duration: 0.12)) }

	// The profile sheet sliding in from the right as its scrim fades in, and
	// back out.
	static var sheet: Animation? { unlessReduced(.easeOut(duration: 0.2)) }

	// The PIN screen rising, slowing hard as it lands, and sinking back.
	static var lockIn: Animation? { unlessReduced(.timingCurve(0.16, 1, 0.3, 1, duration: 0.4)) }
	static var lockOut: Animation? { unlessReduced(.easeIn(duration: 0.35)) }

	// A quiet element coming and going: the update pill, the full screen
	// hint.
	static var fade: Animation? { unlessReduced(.easeOut(duration: 0.3)) }

	// A status changing in place: the update's text crossfading.
	static var status: Animation? { unlessReduced(.easeInOut(duration: 0.2)) }

	// A progress bar moving to its new share.
	static var progress: Animation? { unlessReduced(.linear(duration: 0.2)) }

	// A spinner keeps turning while motion is reduced, as the system's do:
	// it is the only sign of the work going on.
	static let spin = Animation.linear(duration: 1).repeatForever(autoreverses: false)

	static var isReduced: Bool {
		NSWorkspace.shared.accessibilityDisplayShouldReduceMotion
	}

	// Any other animation, nil while motion is reduced.
	static func unlessReduced(_ animation: Animation) -> Animation? {
		isReduced ? nil : animation
	}
}
