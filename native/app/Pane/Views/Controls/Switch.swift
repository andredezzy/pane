import SwiftUI

// Switch: h-5 w-9, primary when on, the input color when off, a 16px thumb.
struct PaneSwitchStyle: ToggleStyle {
	func makeBody(configuration: Configuration) -> some View {
		PaneSwitch(configuration: configuration)
	}
}

private struct PaneSwitch: View {
	let configuration: ToggleStyle.Configuration

	@Environment(\.isEnabled) private var isEnabled

	var body: some View {
		HStack(spacing: 8) {
			configuration.label

			Capsule(style: .circular)
				.fill(configuration.isOn ? Palette.primary : Palette.border)
				.frame(width: 36, height: 20)
				.overlay(alignment: configuration.isOn ? .trailing : .leading) {
					Circle()
						.fill(Palette.background)
						.frame(width: 16, height: 16)
						.shadow(color: .black.opacity(0.15), radius: 4, y: 2)
						.padding(2)
				}
				.contentShape(Capsule(style: .circular))
				.onTapGesture {
					withAnimation(Motion.toggle) {
						configuration.isOn.toggle()
					}
				}
		}
		.opacity(isEnabled ? 1 : 0.5)
	}
}

extension ToggleStyle where Self == PaneSwitchStyle {
	static var paneSwitch: PaneSwitchStyle { PaneSwitchStyle() }
}
