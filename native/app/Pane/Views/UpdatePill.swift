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
				HStack(spacing: 6) {
					UpdateDot(pulsing: updater.status == .downloading)

					Text(updater.label)
						.font(.system(size: 12))
						.lineLimit(1)
						.contentTransition(.opacity)

					if updater.status == .downloading {
						UpdateProgressBar(progress: updater.downloadProgress ?? 0)
							.frame(minWidth: 24, maxWidth: .infinity)
							.padding(.leading, 4)
					}
				}
				.frame(maxWidth: .infinity, alignment: .leading)
				.contentShape(Rectangle())
			}
			.buttonStyle(.plain)
			.foregroundStyle(Palette.accentForeground)
			.accessibilityIdentifier("pane.sidebar.update")

			DismissButton { updater.dismissedVersion = updater.available?.version }
				.opacity(isHovered ? 1 : 0)
				.padding(.trailing, 6)
		}
		.padding(.vertical, 6)
		.padding(.leading, 8)
		.padding(.trailing, 24)
		.background(Palette.foreground.opacity(0.05), in: RoundedRectangle(cornerRadius: 6, style: .circular))
		.onHover { isHovered = $0 }
		.animation(.easeOut(duration: 0.15), value: isHovered)
		.animation(.easeInOut(duration: 0.2), value: updater.status)
		.accessibilityElement(children: .contain)
		.accessibilityIdentifier("pane.sidebar.updatePill")
	}
}

// The 6px emerald dot; it pulses while the dmg downloads.
struct UpdateDot: View {
	let pulsing: Bool

	@State private var dimmed = false
	@Environment(\.accessibilityReduceMotion) private var reduceMotion

	var body: some View {
		Circle()
			.fill(Color(rgb: 0x10B981))
			.frame(width: 6, height: 6)
			.opacity(pulsing && dimmed ? 0.5 : 1)
			.onAppear { pulse() }
			.onChange(of: pulsing) { pulse() }
	}

	// Tailwind's animate-pulse: half opacity and back, every two seconds.
	private func pulse() {
		guard pulsing, !reduceMotion else {
			dimmed = false
			return
		}

		withAnimation(.easeInOut(duration: 1).repeatForever(autoreverses: true)) {
			dimmed = true
		}
	}
}

// A 4px track at 10% with the emerald share downloaded.
struct UpdateProgressBar: View {
	let progress: Double

	var body: some View {
		GeometryReader { geometry in
			Capsule()
				.fill(Palette.foreground.opacity(0.1))
				.overlay(alignment: .leading) {
					Capsule()
						.fill(Color(rgb: 0x10B981))
						.frame(width: geometry.size.width * min(max(progress, 0), 1))
						.animation(.linear(duration: 0.2), value: progress)
				}
		}
		.frame(height: 4)
	}
}

private struct DismissButton: View {
	let action: () -> Void

	@State private var isHovered = false

	var body: some View {
		Button(action: action) {
			LucideIcon(.x, size: 12)
				.frame(width: 16, height: 16)
				.contentShape(Rectangle())
		}
		.buttonStyle(.plain)
		.foregroundStyle(isHovered ? Palette.accentForeground : Palette.mutedForeground)
		.onHover { isHovered = $0 }
		.accessibilityLabel("Dismiss")
		.accessibilityIdentifier("pane.sidebar.update.dismiss")
	}
}
