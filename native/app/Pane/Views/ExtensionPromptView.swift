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
							.clipShape(Squircle(cornerRadius: 15))
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
		.popoverSurface(cornerRadius: 29)
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

	var body: some View {
		configuration.label
			.font(.system(size: 13, weight: .medium))
			.lineLimit(1)
			.foregroundStyle(isPrimary ? Palette.primaryForeground : Palette.foreground)
			.padding(.horizontal, 14)
			.frame(height: 32)
			.background(fill, in: Squircle(cornerRadius: 15))
			.overlay {
				if !isPrimary {
					Squircle(cornerRadius: 15)
						.strokeBorder(Palette.foreground.opacity(0.08), lineWidth: 1)
				}
			}
			.contentShape(Rectangle())
			.onHover { hovering in
				withAnimation(Motion.hover) { isHovered = hovering }
			}
	}

	private var fill: Color {
		isPrimary ? Palette.primary.opacity(isHovered ? 0.9 : 1) : Palette.secondary.opacity(isHovered ? 0.8 : 1)
	}
}

// Adding an extension, asked as Dia asks (Paper: 02.10 Browser · Extension
// popups, 03.10 Install extension): centered over the window on Quit Pane?'s
// scrim. Pane's icon takes the extension's, then the title, what it can do on
// a tinted panel, and Cancel (Esc) beside Add to Pane (Return). It shows in
// the next frame, as the popups do.
struct ExtensionInstallDialog: View {
	let controller: BrowserController
	let prompt: WindowState.ExtensionPrompt

	// The foreground at 4%, 5% on dark: a panel on the dialog's surface.
	private static let panel = Color(nsColor: NSColor(name: nil) { appearance in
		Palette.isDark(appearance) ? NSColor(white: 0.98, alpha: 0.05) : NSColor(rgb: 0x18181B, alpha: 0.04)
	})

	var body: some View {
		VStack(alignment: .leading, spacing: 0) {
			HStack(spacing: 8) {
				Image(nsImage: NSApp.applicationIconImage)
					.resizable()
					.interpolation(.high)
					.frame(width: 32, height: 32)

				LucideIcon(.arrowLeft, size: 16)
					.foregroundStyle(Palette.mutedForeground)

				Group {
					if let icon = prompt.icon {
						Image(nsImage: icon)
							.resizable()
							.interpolation(.high)
					} else {
						Palette.secondary
							.clipShape(Squircle(cornerRadius: 15))
					}
				}
				.frame(width: 32, height: 32)
			}
			.frame(height: 32)
			.accessibilityHidden(true)

			Text(prompt.title)
				.font(.system(size: 20, weight: .semibold))
				.tracking(-0.2)
				.foregroundStyle(Palette.foreground)
				.fixedSize(horizontal: false, vertical: true)
				.padding(.top, 14)

			if !prompt.permissions.isEmpty {
				VStack(alignment: .leading, spacing: 2) {
					Text(prompt.permissionsHeading)

					ForEach(prompt.permissions, id: \.self) { permission in
						HStack(alignment: .firstTextBaseline, spacing: 8) {
							Text("•")
							Text(permission)
								.fixedSize(horizontal: false, vertical: true)
						}
						.padding(.leading, 8)
					}
				}
				.font(.system(size: 14))
				// 20-point lines.
				.lineSpacing(3)
				.foregroundStyle(Palette.accentForeground)
				.padding(.vertical, 12)
				.padding(.horizontal, 14)
				.frame(maxWidth: .infinity, alignment: .leading)
				.background(Self.panel, in: Squircle(cornerRadius: 15))
				.padding(.top, 12)
			}

			HStack(spacing: 8) {
				Spacer(minLength: 0)

				Button { controller.answerExtensionPrompt(accepted: false) } label: {
					KeyedLabel(title: prompt.cancelTitle, key: "Esc", role: .cancel)
				}
				.buttonStyle(KeyedButtonStyle(role: .cancel))
				.accessibilityIdentifier("pane.extensionPopup.cancel")

				Button { controller.answerExtensionPrompt(accepted: true) } label: {
					KeyedLabel(title: prompt.acceptTitle, key: "↩", role: .primary)
				}
				.buttonStyle(KeyedButtonStyle(role: .primary))
				.accessibilityIdentifier("pane.extensionPopup.accept")
			}
			.padding(.top, 24)
		}
		.padding(24)
		.frame(width: 424, alignment: .leading)
		.surfaceBackground(cornerRadius: 29, shadow: QuitConfirmationView.shadow, blur: 64, y: 24)
		.accessibilityElement(children: .contain)
		.accessibilityAddTraits(.isModal)
		.accessibilityLabel(prompt.title)
		.accessibilityIdentifier("pane.extensionInstall")
	}
}
