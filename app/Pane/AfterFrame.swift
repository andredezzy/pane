import CoreFoundation
import Foundation

// Work for after the frame being built. Core Animation commits a turn's
// changes as the main run loop is about to wait, in an observer of order
// 2000000; blocks queued on the main queue may run before that commit, in the
// same turn, so they cannot tell the frame is drawn. An observer just after
// Core Animation's can.
enum AfterFrame {
	// Runs `body` once the frame this turn builds is committed.
	static func committed(_ body: @escaping @MainActor () -> Void) {
		let observer = CFRunLoopObserverCreateWithHandler(nil, CFRunLoopActivity.beforeWaiting.rawValue, false, 2_000_001) { _, _ in
			MainActor.assumeIsolated(body)
		}

		CFRunLoopAddObserver(CFRunLoopGetMain(), observer, .commonModes)
	}

	// Runs `body` on the turn after this frame's: what it changes shows in the
	// next frame, and the run loop wakes for it, as it would not for work done
	// as it is about to wait.
	static func next(_ body: @escaping @MainActor () -> Void) {
		committed {
			DispatchQueue.main.async {
				MainActor.assumeIsolated(body)
			}
		}
	}
}
