import AppKit
import SwiftUI

// The Electron app's design tokens (apps/veil/src/renderer/styles/globals.css),
// which the Pane file in Paper documents too: each one a light/dark pair that
// follows the window's appearance.
enum Palette {
	static let background = pair(0xFAFAFA, 0x0A0A0C)
	static let foreground = pair(0x18181B, 0xFAFAFA)
	static let card = pair(0xFFFFFF, 0x1C1C1F)
	static let popover = pair(0xFFFFFF, 0x1C1C1F)
	static let primary = pair(0x18181B, 0xFAFAFA)
	static let primaryForeground = pair(0xFAFAFA, 0x18181B)
	static let secondary = pair(0xE9E9EB, 0x232326)
	static let mutedForeground = pair(0x71717A, 0x71717A)
	static let accent = pair(0xE9E9EB, 0x232326)
	static let accentForeground = pair(0x27272A, 0xE4E4E7)
	static let destructive = pair(0xDC2626, 0xEF4444)
	static let destructiveForeground = pair(0xFAFAFA, 0xFAFAFA)
	static let ring = pair(0xA1A1AA, 0x52525B)
	// Borders and input outlines: black 8% on light, white 6% on dark.
	static let border = Color(nsColor: NSColor(name: nil) { appearance in
		isDark(appearance) ? NSColor(white: 1, alpha: 0.06) : NSColor(white: 0, alpha: 0.08)
	})

	// Scrims stay black in both themes (DESIGN.md: Theme).
	static let sheetScrim = Color.black.opacity(0.4)
	static let dialogScrim = Color.black.opacity(0.6)

	// The 2px loading bar under the address: blue-500 to blue-400.
	static let progress = LinearGradient(
		colors: [Color(rgb: 0x3B82F6), Color(rgb: 0x60A5FA)],
		startPoint: .leading,
		endPoint: .trailing
	)

	private static func pair(_ light: Int, _ dark: Int) -> Color {
		Color(nsColor: NSColor(name: nil) { appearance in
			NSColor(rgb: isDark(appearance) ? dark : light)
		})
	}

	static func isDark(_ appearance: NSAppearance) -> Bool {
		appearance.bestMatch(from: [.aqua, .darkAqua]) == .darkAqua
	}
}

extension NSColor {
	convenience init(rgb: Int, alpha: CGFloat = 1) {
		self.init(
			srgbRed: CGFloat((rgb >> 16) & 0xFF) / 255,
			green: CGFloat((rgb >> 8) & 0xFF) / 255,
			blue: CGFloat(rgb & 0xFF) / 255,
			alpha: alpha
		)
	}
}

extension Color {
	init(rgb: Int) {
		self.init(nsColor: NSColor(rgb: rgb))
	}
}

// A CSS line box: single-line text centered in the line height the Electron
// app's CSS gives it (text-xs 16, text-sm 20, text-lg 28, leading-none = the
// font size, and 1.5× a size set without a line height).
extension View {
	func lineBox(_ height: CGFloat) -> some View {
		frame(height: height)
	}
}

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

// The design's elevations (Paper › Foundations › Elevation). A CSS blur is
// twice a Core Animation shadow radius.
extension View {
	// A surface's fill with the design's ring and drop shadow, both outside
	// it as CSS box-shadows are: the content panel's (`shadow-panel`, a
	// 1px border ring and 0 4px 24px black 16%, 40% on dark) by default.
	func surfaceBackground(
		_ fill: Color = Palette.card,
		cornerRadius: CGFloat,
		ring: Color = Palette.border,
		shadow: Color = PanelShadow.color,
		blur: CGFloat = 24,
		y: CGFloat = 4
	) -> some View {
		background {
			Squircle(cornerRadius: cornerRadius)
				.fill(fill)
				.shadow(color: shadow, radius: blur / 2, y: y)
				.overlay {
					Squircle(cornerRadius: cornerRadius + 1)
						.strokeBorder(ring, lineWidth: 1)
						.padding(-1)
				}
		}
	}

	// A popover surface with a foreground/10 ring, over shadow-2xl (0 25px 50px
	// -12px black 25%): the tab switcher, the extensions menus, and their
	// popups. The shadow is the surface's alone: on the whole view, SwiftUI
	// shadows each row, text, and fill inside it.
	func popoverSurface(cornerRadius: CGFloat) -> some View {
		background {
			Squircle(cornerRadius: cornerRadius)
				.fill(Palette.popover)
				.shadow(color: .black.opacity(0.25), radius: 25, y: 25)
		}
		.overlay {
			Squircle(cornerRadius: cornerRadius)
				.strokeBorder(Palette.foreground.opacity(0.1), lineWidth: 1)
		}
	}

	// shadow-md: the find bar.
	func mediumShadow() -> some View {
		shadow(color: .black.opacity(0.1), radius: 3, y: 4)
			.shadow(color: .black.opacity(0.1), radius: 2, y: 2)
	}
}

enum PanelShadow {
	static let color = Color(nsColor: NSColor(name: nil) { appearance in
		NSColor(white: 0, alpha: Palette.isDark(appearance) ? 0.4 : 0.16)
	})
}

// Lucide icons (ISC; native/NOTICE), the Electron app's icon set, drawn from
// their own SVG paths as template images so they take the text color.
enum Lucide: String {
	case arrowDown
	case arrowLeft
	case arrowRight
	case arrowUp
	case chevronDown
	case chevronsDownUp
	case circleDashedX
	case globe
	case loaderCircle
	case moon
	case pencil
	case rotateCw
	case settings
	case squareArrowOutUpRight
	case timerOff
	case trash2
	case x

	private var elements: String {
		switch self {
		case .arrowDown:
			#"<path d="M12 5v14"/><path d="m19 12-7 7-7-7"/>"#
		case .arrowLeft:
			#"<path d="m12 19-7-7 7-7"/><path d="M19 12H5"/>"#
		case .arrowRight:
			#"<path d="M5 12h14"/><path d="m12 5 7 7-7 7"/>"#
		case .arrowUp:
			#"<path d="m5 12 7-7 7 7"/><path d="M12 19V5"/>"#
		case .chevronDown:
			#"<path d="m6 9 6 6 6-6"/>"#
		case .circleDashedX:
			#"<circle cx="12" cy="12" r="9" stroke-dasharray="2.6 2.6"/><path d="M9.5 9.5l5 5"/><path d="M14.5 9.5l-5 5"/>"#
		case .chevronsDownUp:
			#"<path d="m7 20 5-5 5 5"/><path d="m7 4 5 5 5-5"/>"#
		case .globe:
			#"<circle cx="12" cy="12" r="10"/><path d="M12 2a14.5 14.5 0 0 0 0 20 14.5 14.5 0 0 0 0-20"/><path d="M2 12h20"/>"#
		case .loaderCircle:
			#"<path d="M21 12a9 9 0 1 1-6.219-8.56"/>"#
		case .moon:
			#"<path d="M20.985 12.486a9 9 0 1 1-9.473-9.472c.405-.022.617.46.402.803a6 6 0 0 0 8.268 8.268c.344-.215.825-.004.803.401"/>"#
		case .pencil:
			#"<path d="M21.174 6.812a1 1 0 0 0-3.986-3.987L3.842 16.174a2 2 0 0 0-.5.83l-1.321 4.352a.5.5 0 0 0 .623.622l4.353-1.32a2 2 0 0 0 .83-.497z"/><path d="m15 5 4 4"/>"#
		case .rotateCw:
			#"<path d="M21 12a9 9 0 1 1-9-9c2.52 0 4.93 1 6.74 2.74L21 8"/><path d="M21 3v5h-5"/>"#
		case .settings:
			#"<path d="M9.671 4.136a2.34 2.34 0 0 1 4.659 0 2.34 2.34 0 0 0 3.319 1.915 2.34 2.34 0 0 1 2.33 4.033 2.34 2.34 0 0 0 0 3.831 2.34 2.34 0 0 1-2.33 4.033 2.34 2.34 0 0 0-3.319 1.915 2.34 2.34 0 0 1-4.659 0 2.34 2.34 0 0 0-3.32-1.915 2.34 2.34 0 0 1-2.33-4.033 2.34 2.34 0 0 0 0-3.831A2.34 2.34 0 0 1 6.35 6.051a2.34 2.34 0 0 0 3.319-1.915"/><circle cx="12" cy="12" r="3"/>"#
		case .squareArrowOutUpRight:
			#"<path d="M21 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h6"/><path d="m21 3-9 9"/><path d="M15 3h6v6"/>"#
		case .timerOff:
			#"<path d="M10 2h4"/><path d="M4.6 11a8 8 0 0 0 1.7 8.7 8 8 0 0 0 8.7 1.7"/><path d="M7.4 7.4a8 8 0 0 1 10.3 1 8 8 0 0 1 .9 10.2"/><path d="m2 2 20 20"/><path d="M12 12v-2"/>"#
		case .trash2:
			#"<path d="M10 11v6"/><path d="M14 11v6"/><path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6"/><path d="M3 6h18"/><path d="M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/>"#
		case .x:
			#"<path d="M18 6 6 18"/><path d="m6 6 12 12"/>"#
		}
	}

	@MainActor
	var image: NSImage {
		image(strokeWidth: 2, size: 24)
	}

	// A menu item's icon, as the Electron app drew them (menu/icons.ts):
	// 16 points, a 1.5 stroke.
	@MainActor
	var menuImage: NSImage {
		image(strokeWidth: 1.5, size: 16)
	}

	@MainActor
	func image(strokeWidth: Double, size: Int) -> NSImage {
		TemplateIcon.image("lucide.\(rawValue)", elements, strokeWidth: strokeWidth, size: size)
	}
}

// Pane's own icons, drawn after Aside's extensions menu: its puzzle (a knob
// on top, a notch on the right), a bell-shaped pin, the pin struck through,
// the arrow onto a line, and three dots. On Lucide's grid (24 units, round
// caps and joins), so they sit with its icons.
enum Glyph: String {
	case download
	case ellipsis
	case pin
	case pinOff
	case puzzle

	private var elements: String {
		switch self {
		case .download:
			#"<path d="M12 3.25v14"/><path d="m7.25 12.5 4.75 4.75 4.75-4.75"/><path d="M5 20.75h14"/>"#
		case .ellipsis:
			#"<circle cx="3.4" cy="12" r="1.9" fill="black" stroke="none"/><circle cx="12" cy="12" r="1.9" fill="black" stroke="none"/><circle cx="20.6" cy="12" r="1.9" fill="black" stroke="none"/>"#
		case .pin:
			#"<path d="M12 14.5V21"/><path d="M8 9V6a2.5 2.5 0 0 1 2.5-2.5h3A2.5 2.5 0 0 1 16 6v3l3.3 5.5H4.7Z"/>"#
		case .pinOff:
			// The pin stops short of the stroke on its upper side, and meets
			// it below.
			#"<path d="M5.5 14.5h-.8L8 9V6a2.5 2.5 0 0 1 2.5-2.5h3a2.5 2.5 0 0 1 2 1"/><path d="M16 8v1l3.3 5.5H9.5"/><path d="M12 14.5V21"/><path d="M4.25 19.75 19.75 4.25"/>"#
		case .puzzle:
			#"<path d="M6 6.25H9.67A2.35 2.35 0 1 1 14.33 6.25H18A2 2 0 0 1 20 8.25V10.88A2.5 2.5 0 0 0 20 15.88V18.5A2 2 0 0 1 18 20.5H6A2 2 0 0 1 4 18.5V8.25A2 2 0 0 1 6 6.25Z"/>"#
		}
	}

	@MainActor
	func image(strokeWidth: Double, size: Int) -> NSImage {
		TemplateIcon.image("glyph.\(rawValue)", elements, strokeWidth: strokeWidth, size: size)
	}
}

// An icon's SVG elements as a template image, `size` points square, so it
// takes the text color. Each name, stroke, and size is drawn once.
@MainActor
private enum TemplateIcon {
	private static var cache: [String: NSImage] = [:]

	static func image(_ name: String, _ elements: String, strokeWidth: Double, size: Int) -> NSImage {
		let key = "\(name) \(strokeWidth) \(size)"

		if let image = cache[key] {
			return image
		}

		let svg = #"<svg xmlns="http://www.w3.org/2000/svg" width="\#(size)" height="\#(size)" viewBox="0 0 24 24" fill="none" stroke="black" stroke-width="\#(strokeWidth)" stroke-linecap="round" stroke-linejoin="round">"#
			+ elements + "</svg>"
		let image = NSImage(data: Data(svg.utf8)) ?? NSImage()
		image.isTemplate = true
		cache[key] = image

		return image
	}
}

// A lucide icon at the design's size (h-3 is 12, h-3.5 is 14, h-4 is 16).
struct LucideIcon: View {
	let icon: Lucide
	let size: CGFloat

	init(_ icon: Lucide, size: CGFloat) {
		self.icon = icon
		self.size = size
	}

	var body: some View {
		Image(nsImage: icon.image)
			.renderingMode(.template)
			.resizable()
			.interpolation(.high)
			.frame(width: size, height: size)
			.accessibilityHidden(true)
	}
}
