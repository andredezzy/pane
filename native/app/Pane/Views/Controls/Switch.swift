import SwiftUI

// Switch: h-5 w-9, primary when on, the input color when off, a 16px thumb.
// The whole row is its button, as a label's click toggles a checkbox: a
// click on the label toggles it too, and so does Space with the keyboard.
// A press darkens the track as a button's fill.
struct PaneSwitchStyle: ToggleStyle {
	func makeBody(configuration: Configuration) -> some View {
		PaneSwitch(configuration: configuration)
	}
}

private struct PaneSwitch: View {
	let configuration: ToggleStyle.Configuration

	@Environment(\.isEnabled) private var isEnabled

	var body: some View {
		Button {
			withAnimation(Motion.toggle) {
				configuration.isOn.toggle()
			}
		} label: {
			configuration.label
		}
		.buttonStyle(SwitchRowStyle(isOn: configuration.isOn))
		.opacity(isEnabled ? 1 : 0.5)
	}
}

private struct SwitchRowStyle: ButtonStyle {
	let isOn: Bool

	func makeBody(configuration: Configuration) -> some View {
		let track = Capsule(style: .circular)

		HStack(spacing: 8) {
			configuration.label

			track
				.fill(isOn ? Palette.primary : Palette.border)
				.overlay {
					PressLayer(shape: track, color: isOn ? Palette.pressOnStrong : Palette.press, isPressed: configuration.isPressed)
				}
				.frame(width: 36, height: 20)
				.overlay(alignment: isOn ? .trailing : .leading) {
					Circle()
						.fill(Palette.background)
						.frame(width: 16, height: 16)
						.shadow(color: .black.opacity(0.15), radius: 4, y: 2)
						.padding(2)
				}
		}
		.contentShape(Rectangle())
	}
}

extension ToggleStyle where Self == PaneSwitchStyle {
	static var paneSwitch: PaneSwitchStyle { PaneSwitchStyle() }
}
