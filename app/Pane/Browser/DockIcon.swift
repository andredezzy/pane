import AppKit
import PaneKit

// Pane's icon in the Dock while it runs (ADR 0089): the window the App icon
// setting names, or for automatic, Day or Night as the app's appearance is
// light or dark. The bundle's own icon is Day, and Night under the system's
// dark icon style; this one holds whatever that style is.
@MainActor
final class DockIcon {
	private var setting = AppIcon.automatic
	// The window on show, so an appearance change that keeps it redraws nothing.
	private(set) var shown: AppIcon?
	private var observation: NSKeyValueObservation?

	init() {
		// The app's appearance follows its theme and, under system, the Mac's.
		observation = NSApp.observe(\.effectiveAppearance) { [weak self] _, _ in
			MainActor.assumeIsolated { self?.apply() }
		}
	}

	func show(_ setting: AppIcon) {
		self.setting = setting
		apply()
	}

	private func apply() {
		let icon = setting.resolved(dark: Palette.isDark(NSApp.effectiveAppearance))

		guard icon != shown, let image = Self.image(icon) else {
			return
		}

		shown = icon
		NSApp.applicationIconImage = image
	}

	// The window as macOS lays out an app icon: its body, margins, and shadow
	// (app/icon/make-icons.swift).
	nonisolated static func image(_ icon: AppIcon) -> NSImage? {
		NSImage(named: "AppIcon\(icon.resolved(dark: false).rawValue.capitalized)")
	}

	// Each choice for Settings' tiles, drawn at whatever size and scale it is
	// shown, on whichever thread draws it: automatic as Day above the diagonal
	// and Night below it.
	static let thumbnails: [AppIcon: NSImage] = Dictionary(uniqueKeysWithValues: AppIcon.allCases.map { icon in
		let thumbnail = NSImage(size: NSSize(width: 56, height: 56), flipped: false) { @Sendable rect in
			guard icon == .automatic else {
				image(icon)?.draw(in: rect)
				return true
			}

			// Night whole, then Day over its half: two clipped halves would
			// leave the background showing along their shared edge.
			image(.night)?.draw(in: rect)
			NSGraphicsContext.saveGraphicsState()
			let half = NSBezierPath()
			half.move(to: NSPoint(x: rect.minX, y: rect.minY))
			half.line(to: NSPoint(x: rect.minX, y: rect.maxY))
			half.line(to: NSPoint(x: rect.maxX, y: rect.maxY))
			half.close()
			half.addClip()
			image(.day)?.draw(in: rect)
			NSGraphicsContext.restoreGraphicsState()
			return true
		}
		thumbnail.accessibilityDescription = icon.rawValue.capitalized
		return (icon, thumbnail)
	})
}
