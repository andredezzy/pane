import SwiftUI

// Find in page: Enter goes to the next match, Shift-Enter to the previous,
// Escape closes.
struct FindBar: View {
	let controller: BrowserController

	@State private var text = ""
	@FocusState private var focused: Bool

	private var find: WindowState.Find { controller.state.find }

	var body: some View {
		HStack(spacing: 6) {
			TextField("Find in page", text: $text)
				.textFieldStyle(.plain)
				.font(.system(size: 12))
				.frame(width: 180)
				.focused($focused)
				.onChange(of: text) { _, value in controller.find(value) }
				.onSubmit { controller.find(text, forward: !NSEvent.modifierFlags.contains(.shift)) }
				.onExitCommand { controller.closeFind() }
				.accessibilityIdentifier("pane.find.field")

			Text(text.isEmpty ? "" : "\(find.activeMatch)/\(find.matches)")
				.font(.system(size: 11).monospacedDigit())
				.foregroundStyle(.secondary)
				.frame(minWidth: 34, alignment: .trailing)
				.accessibilityIdentifier("pane.find.count")

			Button { controller.find(text, forward: false) } label: { Image(systemName: "chevron.up") }
				.buttonStyle(.plain)
				.accessibilityLabel("Previous match")
			Button { controller.find(text, forward: true) } label: { Image(systemName: "chevron.down") }
				.buttonStyle(.plain)
				.accessibilityLabel("Next match")
			Button { controller.closeFind() } label: { Image(systemName: "xmark") }
				.buttonStyle(.plain)
				.accessibilityLabel("Close find bar")
		}
		.font(.system(size: 11, weight: .medium))
		.foregroundStyle(.secondary)
		.padding(.horizontal, 10)
		.padding(.vertical, 7)
		.background(.regularMaterial, in: RoundedRectangle(cornerRadius: 8, style: .continuous))
		.shadow(color: .black.opacity(0.12), radius: 6, y: 2)
		.padding(10)
		// Focus set during onAppear is dropped: the field is not in the window
		// yet. One turn of the main loop later it is.
		.onAppear { DispatchQueue.main.async { focused = true } }
		.onChange(of: controller.state.findFocusRequest) { focused = true }
		.accessibilityIdentifier("pane.find")
	}
}
