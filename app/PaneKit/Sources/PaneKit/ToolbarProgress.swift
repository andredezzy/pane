import Foundation

// The address bar's load progress as Chrome's own toolbar shows it: a port of
// Chromium's ToolbarProgressBar (chrome/browser/ui/android/toolbar) as stable
// Chrome runs it, its composited animation off by default, with the rules of
// its LoadProgressMediator for what moves it, its ProgressAnimationSmooth, and
// its ToolbarProgressBarAnimatingView (the user's call: exactly as a stable
// browser does it).
//
// A load shows its real progress, at least 5%, the bar jumping to each new
// value; it fades in over 140 ms. Five seconds with no news start its slow
// mode: a shimmer crosses the filled part and the fill moves smoothly, on a
// constant acceleration and deceleration. A finished load fills the bar,
// holds 100 ms, and fades out over 140 ms; a tab come forward with no load,
// or a crash, hides it at once. Time comes in (`frame`), and the drawing goes
// out: the share filled, the opacity, and the shimmer's span.
@MainActor
public final class ToolbarProgress {
	// LoadProgressMediator.MINIMUM_LOAD_PROGRESS.
	static let minimumProgress = 0.05
	// ToolbarProgressBar: ANIMATION_START_THRESHOLD, HIDE_DELAY_MS,
	// ALPHA_ANIMATION_DURATION_MS, PROGRESS_FRAME_TIME_CAP_MS.
	static let slowModeAfter: TimeInterval = 5
	static let hideDelay: TimeInterval = 0.1
	static let fadeDuration: TimeInterval = 0.14
	static let frameTimeCap: TimeInterval = 0.05

	// What to draw.
	public private(set) var progress: Double = 0
	public var isStarted: Bool { started }

	// The bar's width in points and the screen's pixels per point: the
	// smooth fill rounds to a pixel, and the shimmer spans the filled part.
	public var width: Double = 0 {
		didSet {
			if shimmer.isRunning {
				shimmer.progressWidth = progress * width
			}
		}
	}

	public var scale: Double = 2
	// While the Mac reduces motion, the fades are instant.
	public var reducesMotion = false
	// Called after each change of what to draw.
	public var onChange: (() -> Void)?

	private var started = false
	private var target: Double = 0
	private var completion = Completion.finishedDontAnimate
	// The mediator's last progress (LoadProgressProperties.PROGRESS).
	private var loadProgress: Double?
	private var smooth = SmoothAnimation()
	private var smoothRunning = false
	private var lastFrame: TimeInterval?
	private var slowModeAt: TimeInterval?
	private var hideAt: TimeInterval?
	private var fade = Fade(value: 0)
	private var shimmer = Shimmer()

	// LoadProgressProperties.CompletionState.
	private enum Completion {
		case unfinished
		case finishedDoAnimate
		case finishedDontAnimate
	}

	public init() {}

	public func alpha(at now: TimeInterval) -> Double {
		fade.value(at: now)
	}

	// The shimmer's span in points from the bar's leading edge, and its
	// opacity; nil while it does not run.
	public func shimmer(at now: TimeInterval) -> (from: Double, to: Double, alpha: Double)? {
		shimmer.span(at: now).map { ($0.from, $0.to, shimmer.alpha(at: now)) }
	}

	// Whether a frame changes anything: a fade, the slow mode.
	public func needsFrames(at now: TimeInterval) -> Bool {
		smoothRunning || shimmer.isRunning || fade.isRunning(at: now)
	}

	// The next moment something is due with no frame asking: the slow mode,
	// or the hide after a finish.
	public var nextDeadline: TimeInterval? {
		[slowModeAt, hideAt].compactMap(\.self).min()
	}

	// MARK: What moves it (LoadProgressMediator)

	// Another tab forward: its load as it stands, the bar hidden at once
	// when it has none (onNewTabObserved).
	public func tabChanged(progress: Double?, at now: TimeInterval) {
		if let progress {
			startLoad(at: now)
			updateLoad(progress, at: now)
		} else {
			finishLoad(animate: false, at: now)
		}

		onChange?()
	}

	// The selected tab's progress, nil while it does not load a new document.
	public func loadingChanged(from old: Double?, to new: Double?, at now: TimeInterval) {
		switch (old, new) {
		case let (nil, new?):
			// onDidStartNavigationInPrimaryMainFrame.
			startLoad(at: now)
			updateLoad(new, at: now)

		case let (_?, new?):
			// onLoadProgressChanged: a load still on after the bar finished
			// starts it again.
			if new < 1, completion != .unfinished {
				startLoad(at: now)
			}

			updateLoad(new, at: now)

		case let (old?, nil):
			// onLoadStopped: some progress made fills the bar first.
			if old > Self.minimumProgress, old < 1 {
				updateLoad(1, at: now)
			}

			finishLoad(animate: true, at: now)

		case (nil, nil):
			return
		}

		onChange?()
	}

	// onCrash.
	public func crashed(at now: TimeInterval) {
		finishLoad(animate: false, at: now)
		onChange?()
	}

	// The mediator's model notifies on a change of value alone. Its progress
	// starts again with each load here: Chrome keeps it, so a load starting
	// at the value the last one ended at drew an empty bar until its next.
	private func startLoad(at now: TimeInterval) {
		guard completion != .unfinished else {
			return
		}

		completion = .unfinished
		start(at: now)
	}

	private func updateLoad(_ progress: Double, at now: TimeInterval) {
		let progress = max(progress, Self.minimumProgress)

		if loadProgress.map({ abs($0 - progress) >= Self.epsilon }) ?? true {
			loadProgress = progress
			setProgress(progress, at: now)
		}

		if abs(progress - 1) < Self.epsilon {
			finishLoad(animate: true, at: now)
		}
	}

	private func finishLoad(animate: Bool, at now: TimeInterval) {
		let state: Completion = animate ? .finishedDoAnimate : .finishedDontAnimate

		guard state != completion else {
			return
		}

		completion = state
		loadProgress = nil
		finish(fadeOut: animate, at: now)
	}

	// MARK: The bar (ToolbarProgressBar)

	private func start(at now: TimeInterval) {
		started = true
		slowModeAt = now + Self.slowModeAfter
		progress = 0
		smooth.reset(0)
		animateAlpha(to: 1, at: now)
	}

	private func setProgress(_ value: Double, at now: TimeInterval) {
		guard started, abs(target - value) >= Self.epsilon else {
			return
		}

		target = value
		slowModeAt = nil

		if !smoothRunning {
			slowModeAt = now + Self.slowModeAfter
			progress = target
		}

		if value >= 1 - Self.epsilon {
			finish(fadeOut: true, at: now)
		}
	}

	private func finish(fadeOut: Bool, at now: TimeInterval) {
		if abs(progress - 1) >= Self.epsilon {
			setProgress(1, at: now)

			if smoothRunning, fadeOut {
				return
			}
		}

		started = false
		target = 0
		slowModeAt = nil
		shimmer.cancel()
		smoothRunning = false
		lastFrame = nil

		if fadeOut {
			hideAt = now + Self.hideDelay
		} else {
			hideAt = nil
			hide(animated: false, at: now)
		}
	}

	private func hide(animated: Bool, at now: TimeInterval) {
		guard !started else {
			return
		}

		if animated {
			animateAlpha(to: 0, at: now)
		} else {
			fade = Fade(value: 0)
		}
	}

	// LINEAR_OUT_SLOW_IN in, FAST_OUT_LINEAR_IN out, for as long as the
	// share of the way left of 140 ms.
	private func animateAlpha(to value: Double, at now: TimeInterval) {
		let current = fade.value(at: now)
		let difference = value - current

		guard difference != 0 else {
			return
		}

		let duration = reducesMotion ? 0 : abs(difference) * Self.fadeDuration
		fade = Fade(from: current, to: value, start: now, duration: duration, curve: difference > 0 ? .linearOutSlowIn : .fastOutLinearIn)
	}

	// The deadlines due, then the slow mode's step.
	public func frame(at now: TimeInterval) {
		if let at = hideAt, at <= now {
			hideAt = nil
			hide(animated: true, at: at)
		}

		if let at = slowModeAt, at <= now {
			slowModeAt = nil
			startSlowMode(at: at)
		}

		if smoothRunning {
			let elapsed = min(now - (lastFrame ?? now), Self.frameTimeCap)
			lastFrame = now

			if abs(progress - target) >= Self.epsilon {
				progress = max(smooth.update(target: target, frameTime: elapsed, resolution: width * scale), 0)
				shimmer.progressWidth = progress * width

				if abs(progress - 1) < Self.epsilon {
					finish(fadeOut: true, at: now)
				}
			}
		}

		shimmer.step(at: now)
		onChange?()
	}

	// mStartSmoothIndeterminate.
	private func startSlowMode(at now: TimeInterval) {
		guard started else {
			return
		}

		smooth.reset(progress)
		smoothRunning = true
		lastFrame = now
		shimmer.progressWidth = progress * width
		shimmer.start(at: now)
	}

	static let epsilon = 1e-6
}

// ProgressAnimationSmooth: a constant acceleration, then a constant
// deceleration, to stop at the target; a finish (target 1) accelerates hard.
struct SmoothAnimation {
	static let finishingAcceleration = 7.0
	static let acceleration = 0.15
	static let deceleration = 0.15

	private(set) var progress = 0.0
	private var velocity = 0.0

	mutating func reset(_ start: Double) {
		progress = start
		velocity = 0
	}

	mutating func update(target: Double, frameTime: Double, resolution: Double) -> Double {
		let accelerating = acceleratingDuration(target: target, frameTime: frameTime)
		let decelerating = frameTime - accelerating

		if accelerating > 0 {
			let change = (target == 1 ? Self.finishingAcceleration : Self.acceleration) * accelerating
			progress += (velocity + 0.5 * change) * accelerating
			velocity += change
		}

		if decelerating > 0 {
			let change = -Self.deceleration * decelerating
			progress += (velocity + 0.5 * change) * decelerating
			velocity += change
		}

		progress = min(progress, target)

		if target - progress < 0.5 / max(resolution, 1) {
			progress = target
			velocity = 0
		}

		return progress
	}

	// How long it accelerates within the frame, to come to rest at the
	// target: v0 + A dA - D dD = 0, and the distance covered the gap.
	private func acceleratingDuration(target: Double, frameTime: Double) -> Double {
		if target == 1 {
			return frameTime
		}

		let a = Self.acceleration
		let d = Self.deceleration
		let longest = -1 / a * velocity
			+ ((2 * d / ((d + a) * a)) * (target - progress) + (d / ((d + a) * a * a)) * velocity * velocity).squareRoot()

		return max(0, min(frameTime, longest))
	}
}

// ToolbarProgressBarAnimatingView: a block crossing the filled part, slowly
// then fast, a second apart. It grows from 10% to 30% of the filled width
// over the first 60% of the slow pass, at most 400 points, and fades in over
// 500 ms. A pass lasts ln(width) × 260 ms: 60% slow, 2% pause, 38% fast.
struct Shimmer {
	static let slowFraction = 0.60
	static let fastDelayFraction = 0.02
	static let fastFraction = 0.38
	static let delay: TimeInterval = 1
	static let barScale = 0.3
	static let smallBarScale = 0.1
	static let smallBarExpansionComplete = 0.6
	static let maximumWidth = 400.0
	static let fadeIn: TimeInterval = 0.5

	var progressWidth = 0.0
	private(set) var isRunning = false
	private var fade = Fade(value: 0)
	// The pass under way: when its slow part starts and how long each part is.
	private var slowStart: TimeInterval = 0
	private var slowDuration: TimeInterval = 0.3
	private var fastDelay: TimeInterval = 0
	private var fastDuration: TimeInterval = 0.3
	private var fastStart: TimeInterval = 0
	private var slowEnded = false
	// The last animation updated, and its fraction (mLastUpdatedAnimation).
	private var lastIsSlow = true
	private var lastFraction = 0.0

	mutating func start(at now: TimeInterval) {
		guard !isRunning else {
			return
		}

		isRunning = true
		updateDurations()
		slowStart = now
		slowEnded = false
		lastIsSlow = true
		lastFraction = 0
		fade = Fade(from: fade.value(at: now), to: 1, start: now, duration: Self.fadeIn, curve: .linearOutSlowIn)
	}

	mutating func cancel() {
		guard isRunning else {
			return
		}

		isRunning = false
		fade = Fade(value: 0)
		lastFraction = 0
		progressWidth = 0
	}

	func alpha(at now: TimeInterval) -> Double {
		fade.value(at: now)
	}

	// The pass's durations for the filled width now (updateAnimationDuration).
	private mutating func updateDurations() {
		guard progressWidth > 0 else {
			return
		}

		let total = log(progressWidth) * 0.26

		guard total > 0 else {
			return
		}

		slowDuration = total * Self.slowFraction
		fastDelay = total * Self.fastDelayFraction
		fastDuration = total * Self.fastFraction
	}

	mutating func step(at now: TimeInterval) {
		guard isRunning else {
			return
		}

		if !slowEnded {
			guard now >= slowStart else {
				return
			}

			let fraction = min((now - slowStart) / slowDuration, 1)
			lastIsSlow = true
			lastFraction = fraction

			if fraction >= 1 {
				slowEnded = true
				// Each part's end takes the width then.
				updateDurations()
				fastStart = slowStart + slowDuration + fastDelay
			}

			return
		}

		guard now >= fastStart else {
			return
		}

		let fraction = min((now - fastStart) / fastDuration, 1)
		lastIsSlow = false
		lastFraction = fraction

		if fraction >= 1 {
			// The next pass, a second on.
			updateDurations()
			slowStart = fastStart + fastDuration + Self.delay
			slowEnded = false
		}
	}

	// Where the block is (updateAnimation), clipped to the filled part.
	func span(at now: TimeInterval) -> (from: Double, to: Double)? {
		guard isRunning, progressWidth > 0 else {
			return nil
		}

		let fraction = lastFraction
		let interpolated = UnitBezier.fastOutLinearIn.value(at: fraction)
		var scale = Self.barScale

		if lastIsSlow, fraction <= Self.smallBarExpansionComplete {
			scale = Self.smallBarScale + (Self.barScale - Self.smallBarScale) * (fraction / Self.smallBarExpansionComplete)
		}

		var width = min(Self.maximumWidth, progressWidth * scale)
		var center = (progressWidth + width) * interpolated - width / 2
		let right = center + width / 2
		let left = center - width / 2

		if right > progressWidth {
			width -= right - progressWidth
			center -= (right - progressWidth) / 2
		} else if left < 0 {
			width -= -left
			center += -left / 2
		}

		return (center - width / 2, center + width / 2)
	}
}

// An opacity animating from one value to another, or holding one.
struct Fade {
	private let from: Double
	private let to: Double
	private let start: TimeInterval
	private let duration: TimeInterval
	private let curve: UnitBezier

	init(value: Double) {
		self.init(from: value, to: value, start: 0, duration: 0, curve: .linearOutSlowIn)
	}

	init(from: Double, to: Double, start: TimeInterval, duration: TimeInterval, curve: UnitBezier) {
		self.from = from
		self.to = to
		self.start = start
		self.duration = duration
		self.curve = curve
	}

	func value(at now: TimeInterval) -> Double {
		guard duration > 0, now < start + duration else {
			return to
		}

		let fraction = max(now - start, 0) / duration
		return from + (to - from) * curve.value(at: fraction)
	}

	func isRunning(at now: TimeInterval) -> Bool {
		duration > 0 && now < start + duration
	}
}

// A cubic Bézier timing curve, as CSS and Android's PathInterpolator take it.
struct UnitBezier {
	// Material's: Android's Interpolators.LINEAR_OUT_SLOW_IN and
	// FAST_OUT_LINEAR_IN.
	static let linearOutSlowIn = UnitBezier(0, 0, 0.2, 1)
	static let fastOutLinearIn = UnitBezier(0.4, 0, 1, 1)

	private let cx, bx, ax, cy, by, ay: Double

	init(_ x1: Double, _ y1: Double, _ x2: Double, _ y2: Double) {
		cx = 3 * x1
		bx = 3 * (x2 - x1) - cx
		ax = 1 - cx - bx
		cy = 3 * y1
		by = 3 * (y2 - y1) - cy
		ay = 1 - cy - by
	}

	func value(at x: Double) -> Double {
		let x = min(max(x, 0), 1)
		return sampleY(solve(x))
	}

	private func sampleX(_ t: Double) -> Double { ((ax * t + bx) * t + cx) * t }
	private func sampleY(_ t: Double) -> Double { ((ay * t + by) * t + cy) * t }
	private func slopeX(_ t: Double) -> Double { (3 * ax * t + 2 * bx) * t + cx }

	// The curve's parameter at `x`: Newton's method, then halving.
	private func solve(_ x: Double) -> Double {
		var t = x

		for _ in 0 ..< 8 {
			let error = sampleX(t) - x

			if abs(error) < 1e-7 {
				return t
			}

			let slope = slopeX(t)

			if abs(slope) < 1e-6 {
				break
			}

			t -= error / slope
		}

		var low = 0.0
		var high = 1.0
		t = x

		while low < high {
			let value = sampleX(t)

			if abs(value - x) < 1e-7 {
				return t
			}

			if x > value {
				low = t
			} else {
				high = t
			}

			t = (high - low) / 2 + low

			if high - low < 1e-9 {
				break
			}
		}

		return t
	}
}
