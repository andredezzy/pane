import AppKit
import SwiftUI

// Lucide icons (ISC; native/NOTICE), the Electron app's icon set, drawn from
// their own SVG paths as template images so they take the text color.
enum Lucide: String {
	case arrowLeft
	case arrowRight
	case chevronDown
	case chevronLeft
	case chevronRight
	case chevronUp
	case chevronsDownUp
	case chevronsUpDown
	case circleDashedX
	case globe
	case loaderCircle
	case monitor
	case moon
	case pencil
	case rotateCw
	case search
	case settings
	case squareArrowOutUpRight
	case sun
	case timerOff
	case trash2
	case x

	private var elements: String {
		switch self {
		case .arrowLeft:
			#"<path d="m12 19-7-7 7-7"/><path d="M19 12H5"/>"#
		case .arrowRight:
			#"<path d="M5 12h14"/><path d="m12 5 7 7-7 7"/>"#
		case .chevronDown:
			#"<path d="m6 9 6 6 6-6"/>"#
		case .chevronLeft:
			#"<path d="m15 18-6-6 6-6"/>"#
		case .chevronRight:
			#"<path d="m9 18 6-6-6-6"/>"#
		case .chevronUp:
			#"<path d="m18 15-6-6-6 6"/>"#
		case .circleDashedX:
			#"<circle cx="12" cy="12" r="9" stroke-dasharray="2.6 2.6"/><path d="M9.5 9.5l5 5"/><path d="M14.5 9.5l-5 5"/>"#
		case .chevronsDownUp:
			#"<path d="m7 20 5-5 5 5"/><path d="m7 4 5 5 5-5"/>"#
		case .chevronsUpDown:
			#"<path d="m7 15 5 5 5-5"/><path d="m7 9 5-5 5 5"/>"#
		case .globe:
			#"<circle cx="12" cy="12" r="10"/><path d="M12 2a14.5 14.5 0 0 0 0 20 14.5 14.5 0 0 0 0-20"/><path d="M2 12h20"/>"#
		case .loaderCircle:
			#"<path d="M21 12a9 9 0 1 1-6.219-8.56"/>"#
		case .monitor:
			#"<rect width="20" height="14" x="2" y="3" rx="2"/><path d="M8 21h8"/><path d="M12 17v4"/>"#
		case .moon:
			#"<path d="M20.985 12.486a9 9 0 1 1-9.473-9.472c.405-.022.617.46.402.803a6 6 0 0 0 8.268 8.268c.344-.215.825-.004.803.401"/>"#
		case .pencil:
			#"<path d="M21.174 6.812a1 1 0 0 0-3.986-3.987L3.842 16.174a2 2 0 0 0-.5.83l-1.321 4.352a.5.5 0 0 0 .623.622l4.353-1.32a2 2 0 0 0 .83-.497z"/><path d="m15 5 4 4"/>"#
		case .rotateCw:
			#"<path d="M21 12a9 9 0 1 1-9-9c2.52 0 4.93 1 6.74 2.74L21 8"/><path d="M21 3v5h-5"/>"#
		case .search:
			#"<path d="m21 21-4.34-4.34"/><circle cx="11" cy="11" r="8"/>"#
		case .settings:
			#"<path d="M9.671 4.136a2.34 2.34 0 0 1 4.659 0 2.34 2.34 0 0 0 3.319 1.915 2.34 2.34 0 0 1 2.33 4.033 2.34 2.34 0 0 0 0 3.831 2.34 2.34 0 0 1-2.33 4.033 2.34 2.34 0 0 0-3.319 1.915 2.34 2.34 0 0 1-4.659 0 2.34 2.34 0 0 0-3.32-1.915 2.34 2.34 0 0 1-2.33-4.033 2.34 2.34 0 0 0 0-3.831A2.34 2.34 0 0 1 6.35 6.051a2.34 2.34 0 0 0 3.319-1.915"/><circle cx="12" cy="12" r="3"/>"#
		case .squareArrowOutUpRight:
			#"<path d="M21 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h6"/><path d="m21 3-9 9"/><path d="M15 3h6v6"/>"#
		case .sun:
			#"<circle cx="12" cy="12" r="4"/><path d="M12 2v2"/><path d="M12 20v2"/><path d="m4.93 4.93 1.41 1.41"/><path d="m17.66 17.66 1.41 1.41"/><path d="M2 12h2"/><path d="M20 12h2"/><path d="m6.34 17.66-1.41 1.41"/><path d="m19.07 4.93-1.41 1.41"/>"#
		case .timerOff:
			#"<path d="M10 2h4"/><path d="M4.6 11a8 8 0 0 0 1.7 8.7 8 8 0 0 0 8.7 1.7"/><path d="M7.4 7.4a8 8 0 0 1 10.3 1 8 8 0 0 1 .9 10.2"/><path d="m2 2 20 20"/><path d="M12 12v-2"/>"#
		case .trash2:
			#"<path d="M10 11v6"/><path d="M14 11v6"/><path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6"/><path d="M3 6h18"/><path d="M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/>"#
		case .x:
			#"<path d="M18 6 6 18"/><path d="m6 6 12 12"/>"#
		}
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
	// Of the 24-unit grid: 2 is Lucide's own.
	let strokeWidth: Double

	init(_ icon: Lucide, size: CGFloat, strokeWidth: Double = 2) {
		self.icon = icon
		self.size = size
		self.strokeWidth = strokeWidth
	}

	var body: some View {
		Image(nsImage: icon.image(strokeWidth: strokeWidth, size: 24))
			.renderingMode(.template)
			.resizable()
			.interpolation(.high)
			.frame(width: size, height: size)
			.accessibilityHidden(true)
	}
}
