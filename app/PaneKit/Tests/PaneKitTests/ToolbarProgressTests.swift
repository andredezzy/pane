@testable import PaneKit
import Testing

// The address bar's progress, as Chrome's toolbar shows it (ToolbarProgressBar,
// its LoadProgressMediator, ProgressAnimationSmooth, and the shimmer), at
// times set by hand.
@MainActor
@Suite("ToolbarProgress")
struct ToolbarProgressTests {
	private func bar() -> ToolbarProgress {
		let bar = ToolbarProgress()
		bar.width = 300
		return bar
	}

	@Test("a load shows at once, at least 5%, and fades in over 140 ms")
	func startsAtOnce() {
		let bar = bar()
		bar.loadingChanged(from: nil, to: 0.02, at: 10)

		#expect(bar.isStarted)
		#expect(bar.progress == 0.05)
		#expect(bar.alpha(at: 10) == 0)
		#expect(bar.alpha(at: 10.07) > 0.5)
		#expect(bar.alpha(at: 10.14) == 1)
	}

	@Test("each new value of a load jumps the bar to it")
	func jumps() {
		let bar = bar()
		bar.loadingChanged(from: nil, to: 0.1, at: 0)
		bar.loadingChanged(from: 0.1, to: 0.6, at: 0.5)

		#expect(bar.progress == 0.6)
	}

	@Test("a finished load fills the bar, holds 100 ms, and fades out over 140 ms")
	func finishes() {
		let bar = bar()
		bar.loadingChanged(from: nil, to: 0.1, at: 0)
		bar.loadingChanged(from: 0.1, to: 0.6, at: 1)
		bar.loadingChanged(from: 0.6, to: nil, at: 2)

		#expect(!bar.isStarted)
		#expect(bar.progress == 1)
		#expect(bar.nextDeadline == 2.1)

		bar.frame(at: 2.05)
		#expect(bar.alpha(at: 2.05) == 1)

		bar.frame(at: 2.1)
		#expect(bar.alpha(at: 2.17) > 0)
		#expect(bar.alpha(at: 2.17) < 1)
		#expect(bar.alpha(at: 2.24) == 0)
	}

	@Test("a tab come forward with no load hides the bar at once")
	func hidesForAnIdleTab() {
		let bar = bar()
		bar.loadingChanged(from: nil, to: 0.3, at: 0)
		bar.tabChanged(progress: nil, at: 1)

		#expect(!bar.isStarted)
		#expect(bar.alpha(at: 1) == 0)
		#expect(bar.nextDeadline == nil)
	}

	@Test("a tab come forward mid-load shows its own progress")
	func showsALoadingTab() {
		let bar = bar()
		bar.loadingChanged(from: nil, to: 0.7, at: 0)
		bar.tabChanged(progress: 0.2, at: 1)

		#expect(bar.isStarted)
		#expect(bar.progress == 0.2)
		#expect(bar.alpha(at: 1) == 1)
	}

	@Test("a crash hides the bar at once")
	func hidesOnACrash() {
		let bar = bar()
		bar.loadingChanged(from: nil, to: 0.3, at: 0)
		bar.crashed(at: 1)

		#expect(bar.alpha(at: 1) == 0)
	}

	@Test("five seconds with no news start the slow mode: a shimmer, and a fill that moves smoothly")
	func slowMode() {
		let bar = bar()
		bar.loadingChanged(from: nil, to: 0.3, at: 0)
		bar.frame(at: 4.9)
		#expect(!bar.needsFrames(at: 4.9))

		bar.frame(at: 5)
		#expect(bar.needsFrames(at: 5))
		#expect(bar.shimmer(at: 5) != nil)

		// News now moves the fill there smoothly, never past it.
		bar.loadingChanged(from: 0.3, to: 0.8, at: 5.1)
		#expect(bar.progress == 0.3)

		var time = 5.1
		var last = bar.progress

		while time < 20 {
			time += 1.0 / 60
			bar.frame(at: time)
			#expect(bar.progress >= last)
			#expect(bar.progress <= 0.8)
			last = bar.progress
		}

		#expect(bar.progress == 0.8)
	}

	@Test("in the slow mode, a finish races to full before it fades")
	func slowFinish() {
		let bar = bar()
		bar.loadingChanged(from: nil, to: 0.3, at: 0)
		bar.frame(at: 5)
		bar.loadingChanged(from: 0.3, to: nil, at: 6)

		// Still racing to full, the bar is shown.
		#expect(bar.progress < 1)
		#expect(bar.alpha(at: 6) == 1)

		var time = 6.0

		while bar.progress < 1, time < 8 {
			time += 1.0 / 60
			bar.frame(at: time)
		}

		#expect(bar.progress == 1)
		#expect(time < 6.6)
		#expect(!bar.isStarted)
	}

	@Test("the shimmer stays within the filled part and starts empty")
	func shimmerSpan() {
		var shimmer = Shimmer()
		shimmer.progressWidth = 200
		shimmer.start(at: 0)
		shimmer.step(at: 0)

		let first = shimmer.span(at: 0)
		#expect(first.map { $0.to - $0.from } == 0)

		var time = 0.0

		while time < 4 {
			time += 1.0 / 60
			shimmer.step(at: time)

			if let span = shimmer.span(at: time) {
				#expect(span.from >= -1e-9)
				#expect(span.to <= 200 + 1e-9)
				#expect(span.to - span.from <= 200 * Shimmer.barScale + 1e-9)
			}
		}
	}

	@Test("Material's curves, as Android's interpolators draw them")
	func curves() {
		#expect(UnitBezier.linearOutSlowIn.value(at: 0) == 0)
		#expect(abs(UnitBezier.linearOutSlowIn.value(at: 1) - 1) < 1e-6)
		#expect(UnitBezier.linearOutSlowIn.value(at: 0.25) > 0.5)
		#expect(UnitBezier.fastOutLinearIn.value(at: 0.25) < 0.1)
	}
}
