import AppKit
import SwiftUI

// The design's elevations (Paper › Foundations › Elevation). A CSS blur is
// twice a Core Animation shadow radius.
extension View {
	// A surface's fill with the design's ring and drop shadow, both outside
	// it as CSS box-shadows are: the content panel's (`shadow-panel`, a
	// 1px border ring and 0 2px 8px black 8%, 25% on dark) by default.
	func surfaceBackground(
		_ fill: Color = Palette.card,
		cornerRadius: CGFloat,
		ring: Color = Palette.border,
		shadow: Color = PanelShadow.color,
		blur: CGFloat = 8,
		y: CGFloat = 2
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

	// A popover surface with a foreground/10 ring, over a soft shadow (0 6px
	// 20px black 14%, as macOS's menus, and 40% on dark, as Paper draws it):
	// the tab switcher, the extensions menus, and their popups. The shadow is
	// the surface's alone: on the whole view, SwiftUI shadows each row, text,
	// and fill inside it.
	func popoverSurface(cornerRadius: CGFloat) -> some View {
		background {
			Squircle(cornerRadius: cornerRadius)
				.fill(Palette.popover)
				.shadow(color: PopoverShadow.color, radius: 10, y: 6)
		}
		.overlay {
			Squircle(cornerRadius: cornerRadius)
				.strokeBorder(Palette.foreground.opacity(0.1), lineWidth: 1)
		}
	}
}

enum PanelShadow {
	static let color = Color(nsColor: NSColor(name: nil) { appearance in
		NSColor(white: 0, alpha: Palette.isDark(appearance) ? 0.25 : 0.08)
	})
}

enum PopoverShadow {
	static let color = Color(nsColor: NSColor(name: nil) { appearance in
		NSColor(white: 0, alpha: Palette.isDark(appearance) ? 0.4 : 0.14)
	})
}
