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
// rounded-lg, a border, the background color, px-2 py-1, shadow-md: a 176px
// field, the count (red when nothing matches), and 20px buttons.
struct FindBar: View {
	let controller: BrowserController

	@FocusState private var focused: Bool

	private var find: WindowState.Find { controller.state.find }
	private var text: String { find.text }
	private var canStep: Bool { !text.isEmpty && find.matches > 0 }

	var body: some View {
		HStack(spacing: 4) {
			// The query is the window's, so a search started elsewhere (the
			// menu, a script) shows here too; typing searches as you go.
			TextField(
				"",
				// Only a changed query searches: the field writes its text back
				// as it takes focus, and searching the same text again would
				// step to the next match.
				text: Binding(get: { find.text }, set: { if $0 != find.text { controller.find($0) } }),
				prompt: Text("Find in page").foregroundStyle(Palette.mutedForeground)
			)
			.textFieldStyle(.plain)
			.font(.system(size: 12))
			.foregroundStyle(Palette.foreground)
			.frame(width: 176, height: 24)
			.focused($focused)
			.onSubmit { controller.find(text, forward: !NSEvent.modifierFlags.contains(.shift)) }
			.onExitCommand { controller.closeFind() }
			.accessibilityIdentifier("pane.find.field")

			if !text.isEmpty {
				Text("\(find.activeMatch)/\(find.matches)")
					.font(.system(size: 11).monospacedDigit())
					.foregroundStyle(find.matches == 0 ? Palette.destructive : Palette.mutedForeground)
					.accessibilityIdentifier("pane.find.count")
			}

			FindButton(icon: .arrowUp, label: "Previous match") { controller.find(text, forward: false) }
				.disabled(!canStep)
			FindButton(icon: .arrowDown, label: "Next match") { controller.find(text, forward: true) }
				.disabled(!canStep)
			FindButton(icon: .x, label: "Close find bar") { controller.closeFind() }
		}
		.padding(.horizontal, 8)
		.padding(.vertical, 4)
		.background(Palette.background, in: RoundedRectangle(cornerRadius: 8, style: .circular))
		.overlay {
			RoundedRectangle(cornerRadius: 8, style: .circular)
				.strokeBorder(Palette.border, lineWidth: 1)
		}
		.mediumShadow()
		// Focus set during onAppear is dropped: the field is not in the window
		// yet. One turn of the main loop later it is.
		.onAppear { DispatchQueue.main.async { focused = true } }
		.onChange(of: controller.state.findFocusRequest) { focused = true }
		.accessibilityElement(children: .contain)
		.accessibilityIdentifier("pane.find")
	}
}

// 20px, rounded 4, a 12px icon; disabled at 30%.
private struct FindButton: View {
	let icon: Lucide
	let label: String
	let action: () -> Void

	@State private var isHovered = false
	@Environment(\.isEnabled) private var isEnabled
	@Environment(\.accessibilityReduceMotion) private var reduceMotion

	var body: some View {
		Button(action: action) {
			LucideIcon(icon, size: 12)
				.frame(width: 20, height: 20)
				.contentShape(Rectangle())
		}
		.buttonStyle(.plain)
		.foregroundStyle(isHovered ? Palette.accentForeground : Palette.mutedForeground)
		.background(isHovered ? Palette.accent : .clear, in: RoundedRectangle(cornerRadius: 4, style: .circular))
		.opacity(isEnabled ? 1 : 0.3)
		.onHover { hovering in
			withAnimation(reduceMotion ? nil : .easeOut(duration: 0.15)) { isHovered = hovering && isEnabled }
		}
		.accessibilityLabel(label)
	}
}
