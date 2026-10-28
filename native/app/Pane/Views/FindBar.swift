import SwiftUI

// Find in page, as the Electron app's overlay: the bar sits at the window's
// top right (24px in, 64px down), and a click anywhere else closes it.
struct FindOverlay: View {
	let controller: BrowserController

	var body: some View {
		ZStack(alignment: .topTrailing) {
			Color.clear
				.contentShape(Rectangle())
				.onTapGesture { controller.closeFind() }

			FindBar(controller: controller)
				.padding(.top, 64)
				.padding(.trailing, 24)
		}
	}
}

// Enter goes to the next match, Shift-Enter to the previous, Escape closes.
// Aside's bar (the user's call, measured from Aside at 2×): 357 by 54
// points, rounded 15 (Aside's circle of 8, on the scale), a soft shadow and
// no border (a hairline ring in dark, as the popovers'). The field starts 16
// points in, with no placeholder; then the count (red when nothing matches),
// the chevrons 4 apart, and close, each a 32-point circle.
struct FindBar: View {
	let controller: BrowserController

	@FocusState private var focused: Bool

	private var find: WindowState.Find { controller.state.find }
	private var text: String { find.text }
	private var canStep: Bool { !text.isEmpty && find.matches > 0 }

	var body: some View {
		HStack(spacing: 8) {
			// The query is the window's, so a search started elsewhere (the
			// menu, a script) shows here too; typing searches as you go.
			TextField(
				"",
				// Only a changed query searches: the field writes its text back
				// as it takes focus, and searching the same text again would
				// step to the next match.
				text: Binding(get: { find.text }, set: { if $0 != find.text { controller.find($0) } }),
				// Aside's field shows no placeholder; this one stays, unseen,
				// for VoiceOver.
				prompt: Text("Find in page").foregroundStyle(.clear)
			)
			.textFieldStyle(.plain)
			.font(.system(size: 13))
			.foregroundStyle(Palette.foreground)
			// The caret in the text's color, not the system's blue.
			.tint(Palette.foreground)
			.frame(maxWidth: .infinity)
			.focused($focused)
			.onSubmit { controller.find(text, forward: !NSEvent.modifierFlags.contains(.shift)) }
			.onExitCommand { controller.closeFind() }
			.accessibilityLabel("Find in page")
			.accessibilityIdentifier("pane.find.field")

			if !text.isEmpty {
				Text("\(find.activeMatch)/\(find.matches)")
					.font(.system(size: 12).monospacedDigit())
					.foregroundStyle(find.matches == 0 ? Palette.destructive : Palette.mutedForeground)
					.accessibilityIdentifier("pane.find.count")
			}

			HStack(spacing: 0) {
				HStack(spacing: 4) {
					FindButton(icon: .chevronUp, size: 18, strokeWidth: 2, label: "Previous match") { controller.find(text, forward: false) }
						.disabled(!canStep)
					FindButton(icon: .chevronDown, size: 18, strokeWidth: 2, label: "Next match") { controller.find(text, forward: true) }
						.disabled(!canStep)
				}

				FindButton(icon: .x, size: 10, strokeWidth: 2.5, label: "Close find bar") { controller.closeFind() }
			}
		}
		.padding(.leading, 16)
		.padding(.trailing, 6)
		.frame(width: 357, height: 54)
		.background {
			Squircle(cornerRadius: Self.cornerRadius)
				.fill(Palette.popover)
				.shadow(color: Self.shadow, radius: 6, y: 6)
		}
		.overlay {
			Squircle(cornerRadius: Self.cornerRadius)
				.strokeBorder(Self.ring, lineWidth: 1)
		}
		// Focus set during onAppear is dropped: the field is not in the window
		// yet. One turn of the main loop later it is.
		.onAppear { DispatchQueue.main.async { focused = true } }
		.onChange(of: controller.state.findFocusRequest) { focused = true }
		.accessibilityElement(children: .contain)
		.accessibilityIdentifier("pane.find")
	}

	// The scale's 15: Aside's circle of 8 (Squircle.circularEquivalent).
	private static let cornerRadius: CGFloat = 15

	private static let shadow = Color(nsColor: NSColor(name: nil) { appearance in
		NSColor(white: 0, alpha: Palette.isDark(appearance) ? 0.4 : 0.25)
	})

	private static let ring = Color(nsColor: NSColor(name: nil) { appearance in
		Palette.isDark(appearance) ? NSColor(white: 1, alpha: 0.1) : .clear
	})
}

// A 32-point circle, the accent's fill on hover; disabled at 40%.
private struct FindButton: View {
	let icon: Lucide
	let size: CGFloat
	let strokeWidth: Double
	let label: String
	let action: () -> Void

	var body: some View {
		Button(action: action) {
			LucideIcon(icon, size: size, strokeWidth: strokeWidth)
		}
		.buttonStyle(.icon(size: 32))
		.accessibilityLabel(label)
	}
}
