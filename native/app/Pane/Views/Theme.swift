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
			RoundedRectangle(cornerRadius: cornerRadius, style: .circular)
				.fill(fill)
				.shadow(color: shadow, radius: blur / 2, y: y)
				.overlay {
					RoundedRectangle(cornerRadius: cornerRadius + 1, style: .circular)
						.strokeBorder(ring, lineWidth: 1)
						.padding(-1)
				}
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
	case globe
	case loaderCircle
	case moon
	case puzzle
	case rotateCw
	case settings
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
		case .chevronsDownUp:
			#"<path d="m7 20 5-5 5 5"/><path d="m7 4 5 5 5-5"/>"#
		case .globe:
			#"<circle cx="12" cy="12" r="10"/><path d="M12 2a14.5 14.5 0 0 0 0 20 14.5 14.5 0 0 0 0-20"/><path d="M2 12h20"/>"#
		case .loaderCircle:
			#"<path d="M21 12a9 9 0 1 1-6.219-8.56"/>"#
		case .moon:
			#"<path d="M20.985 12.486a9 9 0 1 1-9.473-9.472c.405-.022.617.46.402.803a6 6 0 0 0 8.268 8.268c.344-.215.825-.004.803.401"/>"#
		case .puzzle:
			#"<path d="M15.39 4.39a1 1 0 0 0 1.68-.474 2.5 2.5 0 1 1 3.014 3.015 1 1 0 0 0-.474 1.68l1.683 1.682a2.414 2.414 0 0 1 0 3.414L19.61 15.39a1 1 0 0 1-1.68-.474 2.5 2.5 0 1 0-3.014 3.015 1 1 0 0 1 .474 1.68l-1.683 1.682a2.414 2.414 0 0 1-3.414 0L8.61 19.61a1 1 0 0 0-1.68.474 2.5 2.5 0 1 1-3.014-3.015 1 1 0 0 0 .474-1.68l-1.683-1.682a2.414 2.414 0 0 1 0-3.414L4.39 8.61a1 1 0 0 1 1.68.474 2.5 2.5 0 1 0 3.014-3.015 1 1 0 0 1-.474-1.68l1.683-1.682a2.414 2.414 0 0 1 3.414 0z"/>"#
		case .rotateCw:
			#"<path d="M21 12a9 9 0 1 1-9-9c2.52 0 4.93 1 6.74 2.74L21 8"/><path d="M21 3v5h-5"/>"#
		case .settings:
			#"<path d="M9.671 4.136a2.34 2.34 0 0 1 4.659 0 2.34 2.34 0 0 0 3.319 1.915 2.34 2.34 0 0 1 2.33 4.033 2.34 2.34 0 0 0 0 3.831 2.34 2.34 0 0 1-2.33 4.033 2.34 2.34 0 0 0-3.319 1.915 2.34 2.34 0 0 1-4.659 0 2.34 2.34 0 0 0-3.32-1.915 2.34 2.34 0 0 1-2.33-4.033 2.34 2.34 0 0 0 0-3.831A2.34 2.34 0 0 1 6.35 6.051a2.34 2.34 0 0 0 3.319-1.915"/><circle cx="12" cy="12" r="3"/>"#
		case .x:
			#"<path d="M18 6 6 18"/><path d="m6 6 12 12"/>"#
		}
	}

	@MainActor
	var image: NSImage {
		if let image = Self.cache[self] {
			return image
		}

		let svg = #"<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="black" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">"#
			+ elements + "</svg>"
		let image = NSImage(data: Data(svg.utf8)) ?? NSImage()
		image.isTemplate = true
		Self.cache[self] = image

		return image
	}

	@MainActor private static var cache: [Lucide: NSImage] = [:]
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
