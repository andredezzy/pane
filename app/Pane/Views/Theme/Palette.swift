import AppKit
import PaneKit
import SwiftUI

// The Electron app's design tokens (apps/veil/src/renderer/styles/globals.css),
// which the Pane file in Paper documents too: each one a light/dark pair that
// follows the window's appearance.
enum Palette {
	// The window's own fill (AppKit's, under SwiftUI), and SwiftUI's.
	static let windowBackground = dynamic(0xFAFAFA, 0x0A0A0C)
	static let background = Color(nsColor: windowBackground)
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

	// A border a little stronger on dark: white 10% (the border's 6%), and
	// black 8% on light. The dialogs' Cancel.
	static let hairlineStrong = Color(nsColor: NSColor(name: nil) { appearance in
		isDark(appearance) ? NSColor(white: 1, alpha: 0.1) : NSColor(white: 0, alpha: 0.08)
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

	// Status colors, the same in both themes, as colors on fixed-color
	// surfaces are (DESIGN.md: Theme): a proxy's dot (yellow while it is
	// tested, emerald once connected, red when it failed) and the update's
	// dot and download bar. A connected proxy's test button fills with
	// emerald-600, under white text.
	static let warning = Color(rgb: 0xEAB308)
	static let success = Color(rgb: 0x10B981)
	static let failure = Color(rgb: 0xEF4444)
	static let successFill = Color(rgb: 0x059669)

	// An extension without an icon: the first letter of its name on gray.
	// A toolbar action's badge is outlined in a darker gray.
	static let letterTile = Color(rgb: 0x757575)
	static let badgeRing = Color(rgb: 0x444444)

	// Scrims stay black in both themes (DESIGN.md: Theme).
	static let sheetScrim = Color.black.opacity(0.4)
	// Quit Pane?'s and the install dialog's: a light veil, as Aside's,
	// darker on dark.
	static let modalScrim = Color(nsColor: NSColor(name: nil) { appearance in
		NSColor(white: 0, alpha: isDark(appearance) ? 0.45 : 0.14)
	})
	// Their shadow.
	static let dialogShadow = Color(nsColor: NSColor(name: nil) { appearance in
		NSColor(white: 0, alpha: isDark(appearance) ? 0.5 : 0.16)
	})
	// Quit Pane?'s icon tile: the muted fill at 70% on light, as it is on
	// dark.
	static let iconTile = Color(nsColor: NSColor(name: nil) { appearance in
		isDark(appearance) ? NSColor(rgb: 0x232326) : NSColor(rgb: 0xE9E9EB, alpha: 0.7)
	})
	// The key on Cancel's chip: muted, lighter on dark (zinc-400).
	static let keyLabel = pair(0x71717A, 0xA1A1AA)
	// A panel on a dialog's card: the foreground at 4%, 5% on dark.
	static let panelTint = Color(nsColor: NSColor(name: nil) { appearance in
		isDark(appearance) ? NSColor(white: 0.98, alpha: 0.05) : NSColor(rgb: 0x18181B, alpha: 0.04)
	})

	// A page's dialog (Paper: 04.15 to 04.23), drawn as macOS draws an
	// alert: its gray panel at 95% with a hairline edge, its text, its
	// fields, its gray and blue buttons, and its shadow (0 16px 48px black
	// 25%, in both themes).
	enum Alert {
		static let panel = Color(nsColor: NSColor(name: nil) { appearance in
			isDark(appearance) ? NSColor(rgb: 0x2C2C2E, alpha: 0.95) : NSColor(rgb: 0xEBEBEB, alpha: 0.95)
		})
		static let edge = Color(nsColor: NSColor(name: nil) { appearance in
			isDark(appearance) ? NSColor(white: 1, alpha: 0.14) : NSColor(white: 0, alpha: 0.12)
		})
		// AppKit's, for the fields' text, and SwiftUI's.
		static let textColor = dynamic(0x1D1D1F, 0xF5F5F7)
		static let text = Color(nsColor: textColor)
		static let field = pair(0xFFFFFF, 0x1C1C1E)
		static let fieldEdge = Color(nsColor: NSColor(name: nil) { appearance in
			isDark(appearance) ? NSColor(white: 1, alpha: 0.18) : NSColor(white: 0, alpha: 0.18)
		})
		static let placeholderColor = NSColor(rgb: 0x8E8E93)
		static let button = pair(0xDADADA, 0x48484A)
		static let defaultButton = pair(0x3478F6, 0x0A84FF)
		static let shadow = Color.black.opacity(0.25)
	}

	// The 2px loading bar under the address: blue-500 to blue-400.
	static let progress = LinearGradient(
		colors: [Color(rgb: 0x3B82F6), Color(rgb: 0x60A5FA)],
		startPoint: .leading,
		endPoint: .trailing
	)

	private static func pair(_ light: Int, _ dark: Int) -> Color {
		Color(nsColor: dynamic(light, dark))
	}

	private static func dynamic(_ light: Int, _ dark: Int) -> NSColor {
		NSColor(name: nil) { appearance in
			NSColor(rgb: isDark(appearance) ? dark : light)
		}
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
