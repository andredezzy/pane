import PaneKit
import SwiftUI

// The sidebar footer's update pill (apps/veil sidebar.tsx SidebarUpdatePill):
// quiet by design, no color beyond the status dot. A click downloads; the
// dismiss button, shown on hover, hides it for this version until relaunch
// (DESIGN.md: Updates).
struct UpdatePill: View {
	let updater: Updater

	@State private var isHovered = false

	var body: some View {
		ZStack(alignment: .trailing) {
			Button {
				updater.downloadUpdate()
			} label: {
				HStack(spacing: 10) {
					UpdateDot(pulsing: updater.status == .downloading)

					Text(updater.label)
						// 12, as the sidebar's rows (ADR 0035).
						.font(.system(size: 12))
						.lineLimit(1)
						// Whole beside the bar, which takes what is left.
						.layoutPriority(1)
						.contentTransition(.opacity)
						.animation(updater.status == .downloaded ? Motion.statusAfterBar : Motion.status, value: updater.status)

					if updater.status == .downloading {
						UpdateProgressBar(progress: updater.downloadProgress ?? 0)
							// Clear of the dismiss button while it shows, as
							// far from it as the text is from the bar: it
							// draws back at once, and runs out once the button
							// has gone. Within room it always keeps, 24 points
							// beside that, so the text never moves and the bar
							// never goes.
							.padding(.trailing, isHovered ? 25 : 0)
							.animation(isHovered ? Motion.hover : Motion.hoverTurn, value: isHovered)
							.frame(minWidth: 24 + 25, maxWidth: .infinity)
							.padding(.leading, 4)
							.transition(.asymmetric(insertion: .opacity, removal: .opacity.animation(Motion.barExit)))
					}
				}
				.padding(.leading, 7)
				// Text stays clear of the dismiss button (sidebar.tsx's pr-6).
				// The bar runs out to 7 from the edge, as the dot sits 7 from
				// the start.
				.padding(.trailing, updater.status == .downloading ? 7 : 24)
				.frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .leading)
				.contentShape(Rectangle())
			}
			.buttonStyle(.plain)
			.foregroundStyle(Palette.accentForeground)
			.accessibilityIdentifier("pane.sidebar.update")

			// 6 from the pill's edge (right-1.5). Over a bar, it shows once
			// the bar has drawn back.
			DismissButton(action: updater.dismissPill)
				.opacity(isHovered ? 1 : 0)
				.animation(isHovered && updater.status == .downloading ? Motion.hoverTurn : Motion.hover, value: isHovered)
				.padding(.trailing, 6)
		}
		.frame(height: 32)
		.background(Palette.foreground.opacity(0.05), in: Squircle(cornerRadius: 15))
		.onHover { isHovered = $0 }
		.animation(Motion.hover, value: isHovered)
		.animation(Motion.status, value: updater.status)
		.accessibilityElement(children: .contain)
		.accessibilityIdentifier("pane.sidebar.updatePill")
	}
}

// The 6px emerald dot; it pulses while the dmg downloads.
struct UpdateDot: View {
	let pulsing: Bool

	@State private var dimmed = false

	var body: some View {
		Circle()
			.fill(Palette.success)
			.frame(width: 6, height: 6)
			.opacity(pulsing && dimmed ? 0.5 : 1)
			.onAppear { pulse() }
			.onChange(of: pulsing) { pulse() }
	}

	// Tailwind's animate-pulse: half opacity and back, every two seconds.
	private func pulse() {
		guard pulsing, let pulse = Motion.unlessReduced(.easeInOut(duration: 1).repeatForever(autoreverses: true)) else {
			dimmed = false
			return
		}

		withAnimation(pulse) {
			dimmed = true
		}
	}
}

// A 4px track at 10% with the emerald share downloaded.
struct UpdateProgressBar: View {
	let progress: Double

	var body: some View {
		GeometryReader { geometry in
			Capsule(style: .circular)
				.fill(Palette.foreground.opacity(0.1))
				.overlay(alignment: .leading) {
					Capsule(style: .circular)
						.fill(Palette.success)
						.frame(width: geometry.size.width * min(max(progress, 0), 1))
						.animation(Motion.progress, value: progress)
				}
		}
		.frame(height: 4)
	}
}

// A 12-point X in 16 points, muted until hovered.
private struct DismissButton: View {
	let action: () -> Void

	var body: some View {
		Button(action: action) {
			LucideIcon(.x, size: 12)
		}
		.buttonStyle(.icon(size: 16, shape: .squircle(4), fill: nil))
		.accessibilityLabel("Dismiss")
		.accessibilityIdentifier("pane.sidebar.update.dismiss")
	}
}
