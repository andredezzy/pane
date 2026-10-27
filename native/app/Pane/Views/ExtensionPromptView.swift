import SwiftUI

// Adding or removing an extension, asked as Aside asks (Paper: 02.10 Browser
// · Extension popups): 320 wide under the extension's toolbar button, its
// icon beside the title, then what it can do (adding) or Report abuse
// (removing), and Cancel beside the primary. It is drawn in the window, so it
// shows in the next frame: an AppKit sheet takes some 280 ms to attach, with
// the main thread held. Return accepts and Esc cancels (BrowserController).
struct ExtensionPromptView: View {
	let controller: BrowserController
	let prompt: WindowState.ExtensionPrompt

	var body: some View {
		VStack(alignment: .leading, spacing: 20) {
			HStack(spacing: 20) {
				Group {
					if let icon = prompt.icon {
						Image(nsImage: icon)
							.resizable()
							.interpolation(.high)
					} else {
						Palette.secondary
							.clipShape(RoundedRectangle(cornerRadius: 8, style: .circular))
					}
				}
				.frame(width: 32, height: 32)
				.accessibilityHidden(true)

				Text(prompt.title)
					.font(.system(size: 15, weight: .medium))
					.foregroundStyle(Palette.foreground)
					.fixedSize(horizontal: false, vertical: true)
					.frame(maxWidth: .infinity, alignment: .leading)
			}

			switch prompt.kind {
			case .install where !prompt.permissions.isEmpty:
				VStack(alignment: .leading, spacing: 6) {
					Text(prompt.permissionsHeading)
						.foregroundStyle(Palette.mutedForeground)

					ForEach(prompt.permissions, id: \.self) { permission in
						Text("· \(permission)")
							.foregroundStyle(Palette.accentForeground)
							.fixedSize(horizontal: false, vertical: true)
					}
				}
				.font(.system(size: 13))
				.frame(maxWidth: .infinity, alignment: .leading)
				.padding(.top, 4)

			case .remove:
				Toggle(isOn: Bindable(controller.state).extensionPromptReportsAbuse) {
					Text("Report abuse")
						.font(.system(size: 13))
						.foregroundStyle(Palette.accentForeground)
				}
				.toggleStyle(.checkbox)
				.padding(.top, 4)
				.accessibilityIdentifier("pane.extensionPopup.reportAbuse")

			case .install:
				EmptyView()
			}

			HStack(spacing: 8) {
				Spacer(minLength: 0)

				Button(prompt.cancelTitle) { controller.answerExtensionPrompt(accepted: false) }
					.buttonStyle(PopupButtonStyle(isPrimary: false))
					.accessibilityIdentifier("pane.extensionPopup.cancel")

				Button(prompt.acceptTitle) { controller.answerExtensionPrompt(accepted: true) }
					.buttonStyle(PopupButtonStyle(isPrimary: true))
					.accessibilityIdentifier("pane.extensionPopup.accept")
			}
			.padding(.top, 12)
		}
		.padding(20)
		.frame(width: 320, alignment: .leading)
		.popoverSurface(cornerRadius: 16)
		.accessibilityElement(children: .contain)
		.accessibilityAddTraits(.isModal)
		.accessibilityLabel(prompt.title)
		.accessibilityIdentifier("pane.extensionPopup")
	}
}

// h-8 rounded-lg, 13px medium: Cancel on the secondary fill with a hairline,
// the primary in the primary color. Hover changes color only.
private struct PopupButtonStyle: ButtonStyle {
	let isPrimary: Bool

	func makeBody(configuration: Configuration) -> some View {
		PopupButton(configuration: configuration, isPrimary: isPrimary)
	}
}

private struct PopupButton: View {
	let configuration: ButtonStyle.Configuration
	let isPrimary: Bool

	@State private var isHovered = false
	@Environment(\.accessibilityReduceMotion) private var reduceMotion

	var body: some View {
		configuration.label
			.font(.system(size: 13, weight: .medium))
			.lineLimit(1)
			.foregroundStyle(isPrimary ? Palette.primaryForeground : Palette.foreground)
			.padding(.horizontal, 14)
			.frame(height: 32)
			.background(fill, in: RoundedRectangle(cornerRadius: 8, style: .circular))
			.overlay {
				if !isPrimary {
					RoundedRectangle(cornerRadius: 8, style: .circular)
						.strokeBorder(Palette.foreground.opacity(0.08), lineWidth: 1)
				}
			}
			.contentShape(Rectangle())
			.onHover { hovering in
				withAnimation(reduceMotion ? nil : .easeOut(duration: 0.15)) { isHovered = hovering }
			}
	}

	private var fill: Color {
		isPrimary ? Palette.primary.opacity(isHovered ? 0.9 : 1) : Palette.secondary.opacity(isHovered ? 0.8 : 1)
	}
}
