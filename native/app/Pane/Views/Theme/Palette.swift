import AppKit
import PaneKit
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

	// A press, over a button's fill: the foreground at 6% over a light fill
	// (darker on light, lighter on dark, as the PIN keys' 10%), and the card
	// at 10% over a strong one (primary, destructive), a step past its
	// hover's 90%.
	static let press = foreground.opacity(0.06)
	static let pressOnStrong = card.opacity(0.1)

	// Aside's pinned blue, for an extension's pin: the progress blue,
	// lighter on dark.
	static let pinned = pair(0x3B82F6, 0x60A5FA)

	// Scrims stay black in both themes (DESIGN.md: Theme).
	static let sheetScrim = Color.black.opacity(0.4)

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

	// "#rrggbb"
	init(hex: String) {
		self.init(rgb: Int(hex.dropFirst(), radix: 16) ?? 0)
	}
}

extension ProfileColor {
	var color: Color {
		Color(hex: hex)
	}
}
