import AppKit
import SwiftUI

// A page's dialog (Paper: 03.15 to 03.23), as macOS draws the alert sheet
// it replaced: 260 points wide, the title and the message at 13, the fields,
// then the buttons, the default one blue. Two sit side by side, the default
// on the right; three stack, the default on top. It is drawn in the window,
// so it shows in the next frame with the main thread free; a sheet held it
// some 280 ms. Return presses the default, Esc dismisses (KeyRouter).
struct DialogView: View {
	let dialog: PageDialog
	let presenter: DialogPresenter

	var body: some View {
		VStack(alignment: .leading, spacing: 10) {
			Text(dialog.title)
				.font(.system(size: 13, weight: .bold))
				.frame(minHeight: 16, alignment: .leading)

			if dialog.message.contains(where: { !$0.isEmpty }) {
				message
			}

			if !dialog.fields.isEmpty {
				VStack(spacing: 8) {
					ForEach(Array(dialog.fields.enumerated()), id: \.offset) { index, field in
						DialogField(field: field, index: index, presenter: presenter)
					}
				}
			}

			buttons
				.padding(.top, 6)
		}
		.foregroundStyle(Palette.Alert.text)
		.padding(.top, 22)
		.padding(.leading, 22)
		.padding([.trailing, .bottom], 16)
		.frame(width: 260, alignment: .leading)
		.background {
			Squircle(cornerRadius: 26)
				.fill(Palette.Alert.panel)
				.shadow(color: Palette.Alert.shadow, radius: 24, y: 16)
		}
		.overlay {
			Squircle(cornerRadius: 26)
				.strokeBorder(Palette.Alert.edge, lineWidth: 0.5)
		}
		.accessibilityElement(children: .contain)
		.accessibilityAddTraits(.isModal)
		.accessibilityLabel(dialog.title)
		.accessibilityIdentifier("pane.dialog")
	}

	// A long message scrolls inside the dialog, as an alert's does.
	private var message: some View {
		let lines = VStack(alignment: .leading, spacing: 2) {
			ForEach(Array(dialog.message.enumerated()), id: \.offset) { _, line in
				Text(line)
					.font(.system(size: 13))
					.fixedSize(horizontal: false, vertical: true)
					.frame(maxWidth: .infinity, alignment: .leading)
			}
		}

		return ViewThatFits(in: .vertical) {
			lines

			ScrollView {
				lines
			}
			.frame(maxHeight: 320)
		}
	}

	@ViewBuilder
	private var buttons: some View {
		let titles = dialog.buttons

		if titles.count > 2 {
			VStack(spacing: 8) {
				ForEach(Array(titles.enumerated()), id: \.offset) { index, title in
					button(title, index: index)
				}
			}
		} else {
			HStack(spacing: 8) {
				ForEach(Array(titles.enumerated()).reversed(), id: \.offset) { index, title in
					button(title, index: index)
				}
			}
		}
	}

	private func button(_ title: String, index: Int) -> some View {
		Button(title) { presenter.press(index) }
			.buttonStyle(DialogButtonStyle(isDefault: index == 0))
	}
}

// An alert's text field: white (black on dark) with a gray edge, 22 points
// tall; the edge turns blue while it has the keyboard, and the first takes
// it as the dialog shows. AppKit's field, which the presenter reads as it
// answers: a value set through accessibility (VoiceOver, Voice Control)
// counts, as in the alert's own field.
private struct DialogField: View {
	let field: PageDialog.Field
	let index: Int
	let presenter: DialogPresenter

	@State private var isEditing = false

	var body: some View {
		AlertTextField(field: field, index: index, presenter: presenter, isEditing: $isEditing)
			.padding(.horizontal, 7)
			.frame(height: 22)
			.background(Palette.Alert.field, in: Squircle(cornerRadius: 6))
			.overlay {
				Squircle(cornerRadius: 6)
					.strokeBorder(isEditing ? Palette.Alert.defaultButton : Palette.Alert.fieldEdge, lineWidth: 1)
			}
	}
}

private struct AlertTextField: NSViewRepresentable {
	let field: PageDialog.Field
	let index: Int
	let presenter: DialogPresenter
	@Binding var isEditing: Bool

	func makeNSView(context: Context) -> NSTextField {
		let view = field.isSecure ? NSSecureTextField() : NSTextField()
		view.stringValue = field.text
		view.isBordered = false
		view.drawsBackground = false
		view.focusRingType = .none
		view.font = .systemFont(ofSize: 13)
		view.textColor = Palette.Alert.textColor
		view.lineBreakMode = .byClipping
		view.cell?.isScrollable = true
		view.placeholderAttributedString = NSAttributedString(string: field.placeholder, attributes: [
			.font: NSFont.systemFont(ofSize: 13),
			.foregroundColor: Palette.Alert.placeholderColor,
		])
		view.setAccessibilityLabel(field.placeholder.isEmpty ? "Text" : field.placeholder)
		view.delegate = context.coordinator
		presenter.register(view, at: index)

		// As an alert's first field takes the keyboard, once in the window.
		if index == 0 {
			DispatchQueue.main.async {
				view.window?.makeFirstResponder(view)
			}
		}

		return view
	}

	func updateNSView(_ view: NSTextField, context: Context) {}

	func makeCoordinator() -> Coordinator {
		Coordinator(isEditing: $isEditing)
	}

	final class Coordinator: NSObject, NSTextFieldDelegate {
		let isEditing: Binding<Bool>

		init(isEditing: Binding<Bool>) {
			self.isEditing = isEditing
		}

		func controlTextDidBeginEditing(_ notification: Notification) {
			isEditing.wrappedValue = true
		}

		func controlTextDidEndEditing(_ notification: Notification) {
			isEditing.wrappedValue = false
		}
	}
}

// An alert's button: a 28-point capsule sharing the row, gray, or blue for
// the default. A press darkens it at once and fades as it lets go; an
// alert's buttons show no hover.
private struct DialogButtonStyle: ButtonStyle {
	let isDefault: Bool

	func makeBody(configuration: Configuration) -> some View {
		let shape = Capsule(style: .circular)

		configuration.label
			.font(.system(size: 13))
			.lineLimit(1)
			.foregroundStyle(isDefault ? Color.white : Palette.Alert.text)
			.padding(.horizontal, 10)
			.frame(maxWidth: .infinity)
			.frame(height: 28)
			.background {
				ZStack {
					shape.fill(isDefault ? Palette.Alert.defaultButton : Palette.Alert.button)
					PressLayer(shape: shape, color: Color.black.opacity(0.12), isPressed: configuration.isPressed)
				}
			}
			.contentShape(shape)
	}
}
