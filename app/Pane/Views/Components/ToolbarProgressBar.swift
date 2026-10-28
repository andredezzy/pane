import AppKit
import PaneKit
import SwiftUI

// The address bar's 2-point load progress, drawn as Chrome's toolbar draws it
// (ToolbarProgress) in the blue-500 to blue-400 of the Electron app's bar.
// Each change draws at once. Frames come from the display while a fade or the
// slow mode runs, and a timer wakes the bar for what falls due between them:
// the slow mode after five quiet seconds, the fade after a finish's 100 ms.
struct ToolbarProgressBar: NSViewRepresentable {
	let progress: ToolbarProgress

	func makeNSView(context: Context) -> ToolbarProgressBarView {
		ToolbarProgressBarView(progress: progress)
	}

	func updateNSView(_ view: ToolbarProgressBarView, context: Context) {}
}

final class ToolbarProgressBarView: NSView {
	private let progress: ToolbarProgress
	private let fill = CAGradientLayer()
	private let shimmer = CALayer()
	private var displayLink: CADisplayLink?
	private var wake: Timer?

	init(progress: ToolbarProgress) {
		self.progress = progress
		super.init(frame: .zero)
		wantsLayer = true
		alphaValue = 0

		fill.colors = [NSColor(rgb: 0x3B82F6).cgColor, NSColor(rgb: 0x60A5FA).cgColor]
		fill.startPoint = CGPoint(x: 0, y: 0.5)
		fill.endPoint = CGPoint(x: 1, y: 0.5)
		// Chrome's shimmer: the bar's color with 40% white over it.
		shimmer.backgroundColor = NSColor(rgb: 0x3B82F6).blended(withFraction: 0.4, of: .white)?.cgColor
		layer?.addSublayer(fill)
		layer?.addSublayer(shimmer)

		progress.onChange = { [weak self] in
			self?.draw()
		}
	}

	@available(*, unavailable)
	required init?(coder: NSCoder) {
		fatalError("init(coder:) is not used")
	}

	override func hitTest(_ point: NSPoint) -> NSView? {
		nil
	}

	override func layout() {
		super.layout()
		progress.width = bounds.width
		progress.scale = Double(window?.backingScaleFactor ?? 2)
		draw()
	}

	override func viewDidMoveToWindow() {
		super.viewDidMoveToWindow()

		guard window != nil else {
			displayLink?.invalidate()
			displayLink = nil
			wake?.invalidate()
			return
		}

		// What fell due while the bar was away (Settings covers the toolbar).
		progress.frame(at: CACurrentMediaTime())
	}

	// What the progress says now, with no implicit animation; then the frames
	// and the wake it needs.
	private func draw() {
		let now = CACurrentMediaTime()

		CATransaction.begin()
		CATransaction.setDisableActions(true)

		fill.frame = CGRect(x: 0, y: 0, width: bounds.width * progress.progress, height: bounds.height)

		if let span = progress.shimmer(at: now) {
			shimmer.frame = CGRect(x: span.from, y: 0, width: max(span.to - span.from, 0), height: bounds.height)
			shimmer.opacity = Float(span.alpha)
		} else {
			shimmer.opacity = 0
		}

		alphaValue = progress.alpha(at: now)
		CATransaction.commit()

		schedule(at: now)
	}

	private func schedule(at now: TimeInterval) {
		if progress.needsFrames(at: now), window != nil {
			if displayLink == nil {
				let link = displayLink(target: self, selector: #selector(step))
				link.add(to: .main, forMode: .common)
				displayLink = link
			}

			displayLink?.isPaused = false
		} else {
			displayLink?.isPaused = true
		}

		wake?.invalidate()
		wake = nil

		if let deadline = progress.nextDeadline, window != nil {
			wake = Timer.scheduledTimer(withTimeInterval: max(deadline - now, 0), repeats: false) { [weak self] _ in
				MainActor.assumeIsolated {
					self?.progress.frame(at: CACurrentMediaTime())
				}
			}
		}
	}

	@objc private func step(_ link: CADisplayLink) {
		progress.frame(at: CACurrentMediaTime())
	}
}
