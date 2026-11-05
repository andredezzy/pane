import SwiftUI

// Input: h-9 rounded-md, a 1px input border, px-3 text-sm, muted
// placeholder; focus draws a 1px ring.
struct PaneTextField: View {
	let placeholder: String
	@Binding var text: String
	var monospaced = false
	var fontSize: CGFloat = 14
	var fill = Palette.background
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
		.font(monospaced ? .system(size: 11, design: .monospaced) : .system(size: fontSize))
		.foregroundStyle(Palette.foreground)
		.focused($focused)
		.onSubmit(onSubmit)
		// px-3 inside the 1px border: CSS boxes start the text 13 in.
		.padding(.horizontal, 13)
		.fieldBox(fill: fill, isFocused: focused)
		.opacity(isEnabled ? 1 : 0.5)
		.onAppear {
			if focusesOnAppear {
				// Focus set during onAppear is dropped: the field is not in
				// the window yet. Once the frame that shows it is committed it
				// is, and that frame does not wait for the field editor, which
				// took 30 of the profile sheet's 50 ms to its first frame.
				AfterFrame.next { focused = true }
			}
		}
	}
}

extension View {
	// A field's box: 36 points, rounded 11, its 1-point border, and, focused, the
	// ring 1 point outside the border, as CSS's ring-1 is.
	func fieldBox(fill: Color, isFocused: Bool) -> some View {
		frame(height: 36)
			.background(fill, in: Squircle(cornerRadius: 11))
			.overlay {
				Squircle(cornerRadius: 11)
					.strokeBorder(Palette.border, lineWidth: 1)
			}
			.overlay {
				if isFocused {
					Squircle(cornerRadius: 12)
						.strokeBorder(Palette.ring, lineWidth: 1)
						.padding(-1)
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
