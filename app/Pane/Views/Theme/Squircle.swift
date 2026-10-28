import SwiftUI

// Every rounded corner in Pane (Paper: corner-shape superellipse(2), CSS's
// squircle): in each corner's radius box, the quarter of |x|⁴ + |y|⁴ = 1. It
// insets as a rounded rectangle does, the radius shrinking with the rect, so
// strokeBorder draws a border's inner edge as CSS does.
struct Squircle: InsettableShape {
	var cornerRadius: CGFloat
	private var inset: CGFloat = 0

	init(cornerRadius: CGFloat) {
		self.cornerRadius = cornerRadius
	}

	// The radius animates, so a shape can round off as it grows (the active
	// profile's bar becoming a square).
	var animatableData: CGFloat {
		get { cornerRadius }
		set { cornerRadius = newValue }
	}

	// A circular corner of 0.57 of the radius follows it within 0.012 of the
	// radius (0.12 points at 10): for layers, whose corners can only be
	// circular or Apple's continuous ones.
	static let circularEquivalent: CGFloat = 0.57

	func path(in rect: CGRect) -> Path {
		let rect = rect.insetBy(dx: inset, dy: inset)
		let radius = min(max(cornerRadius - inset, 0), min(rect.width, rect.height) / 2)
		var path = Path()

		guard radius > 0, !rect.isEmpty else {
			path.addRect(rect)
			return path
		}

		// Each corner from the end of one edge to the start of the next,
		// clockwise from the top left: its center (the radius box's inner
		// corner), and the directions to its two edges.
		let corners: [(CGPoint, CGFloat, CGFloat)] = [
			(CGPoint(x: rect.minX + radius, y: rect.minY + radius), -1, -1),
			(CGPoint(x: rect.maxX - radius, y: rect.minY + radius), 1, -1),
			(CGPoint(x: rect.maxX - radius, y: rect.maxY - radius), 1, 1),
			(CGPoint(x: rect.minX + radius, y: rect.maxY - radius), -1, 1),
		]
		// Segments a quarter-point long or less.
		let steps = max(8, Int(radius * 4))

		for (index, (center, dx, dy)) in corners.enumerated() {
			for step in 0 ... steps {
				// From the vertical edge to the horizontal one on the left
				// corners, the other way on the right ones.
				var angle = CGFloat(step) / CGFloat(steps) * .pi / 2
				if index % 2 == 1 {
					angle = .pi / 2 - angle
				}

				let point = CGPoint(
					x: center.x + dx * radius * pow(max(cos(angle), 0), 0.5),
					y: center.y + dy * radius * pow(max(sin(angle), 0), 0.5)
				)

				if index == 0, step == 0 {
					path.move(to: point)
				} else {
					path.addLine(to: point)
				}
			}
		}

		path.closeSubpath()
		return path
	}

	func inset(by amount: CGFloat) -> Squircle {
		var shape = self
		shape.inset += amount
		return shape
	}
}
