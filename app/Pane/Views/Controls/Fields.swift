import SwiftUI

// Input: h-9 rounded-md, a 1px input border, px-3 text-sm, muted
// placeholder; focus draws a 1px ring.
struct PaneTextField: View {
	let placeholder: String
	@Binding var text: String
	var monospaced = false
	// type="password": the text shows as dots.
	var secure = false
	// autoFocus: the field takes focus as it appears.
	var focusesOnAppear = false
	var onSubmit: () -> Void = {}

	@FocusState private var focused: Bool
	@Environment(\.isEnabled) private var isEnabled

	var body: some View {
		field
		.textFieldStyle(.plain)
		.font(monospaced ? .system(size: 11, design: .monospaced) : .system(size: 14))
		.foregroundStyle(Palette.foreground)
		.focused($focused)
		.onSubmit(onSubmit)
		// px-3 inside the 1px border: CSS boxes start the text 13 in.
		.padding(.horizontal, 13)
		.frame(height: 36)
		.background(Palette.background, in: Squircle(cornerRadius: 11))
		.overlay {
			Squircle(cornerRadius: 11)
				.strokeBorder(Palette.border, lineWidth: 1)
		}
		// Focused, the ring 1 point outside the border, as CSS's ring-1 is.
		.overlay {
			if focused {
				Squircle(cornerRadius: 12)
					.strokeBorder(Palette.ring, lineWidth: 1)
					.padding(-1)
			}
		}
		.opacity(isEnabled ? 1 : 0.5)
		.onAppear {
			if focusesOnAppear {
				// Focus set during onAppear is dropped: the field is not in
				// the window yet. One turn of the main loop later it is.
				DispatchQueue.main.async { focused = true }
			}
		}
	}
}

extension PaneTextField {
	@ViewBuilder
	fileprivate var field: some View {
		let prompt = Text(placeholder).foregroundStyle(Palette.mutedForeground)

		if secure {
			SecureField("", text: $text, prompt: prompt)
		} else {
			TextField("", text: $text, prompt: prompt)
		}
	}
}
