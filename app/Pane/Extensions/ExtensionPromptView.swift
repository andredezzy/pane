import SwiftUI

// Adding or removing an extension, asked as Aside asks (Paper: 02.10 Browser
// · Extension popups): 320 wide under the extension's toolbar button, its
// icon beside the title, then what it can do (adding) or Report abuse
// (removing), and Cancel beside the primary. It is drawn in the window, so it
// shows in the next frame: an AppKit sheet takes some 280 ms to attach, with
// the main thread held. Return accepts and Esc cancels (BrowserController).
struct ExtensionPromptView: View {
	let controller: ExtensionsController
	let prompt: ExtensionPrompt

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

			// Installs ask in their own dialog (ExtensionInstallDialog, ADR 0056).
			if prompt.kind == .remove {
				Toggle(isOn: Bindable(controller).promptReportsAbuse) {
					Text("Report abuse")
						.font(.system(size: 13))
						.foregroundStyle(Palette.accentForeground)
				}
				.toggleStyle(.checkbox)
				.padding(.top, 4)
				.accessibilityIdentifier("pane.extensionPopup.reportAbuse")
			}

			HStack(spacing: 8) {
				Spacer(minLength: 0)

				Button(prompt.cancelTitle) { controller.answerPrompt(accepted: false) }
					.buttonStyle(.popup(isPrimary: false))
					.accessibilityIdentifier("pane.extensionPopup.cancel")

				Button(prompt.acceptTitle) { controller.answerPrompt(accepted: true) }
					.buttonStyle(.popup(isPrimary: true))
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

// Adding an extension, asked as Dia asks (Paper: 02.10 Browser · Extension
// popups, 03.10 Install extension): centered over the window on Quit Pane?'s
// scrim. Pane's icon takes the extension's, then the title, what it can do on
// a tinted panel, and Cancel (Esc) beside Add to Pane (Return). It shows in
// the next frame, as the popups do.
struct ExtensionInstallDialog: View {
	let controller: ExtensionsController
	let prompt: ExtensionPrompt

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
				.background(Palette.panelTint, in: Squircle(cornerRadius: 15))
				.padding(.top, 12)
			}

			HStack(spacing: 8) {
				Spacer(minLength: 0)

				Button { controller.answerPrompt(accepted: false) } label: {
					KeyedLabel(title: prompt.cancelTitle, key: "Esc", role: .cancel)
				}
				.buttonStyle(.keyed(.cancel))
				.accessibilityIdentifier("pane.extensionPopup.cancel")

				Button { controller.answerPrompt(accepted: true) } label: {
					KeyedLabel(title: prompt.acceptTitle, key: "↩", role: .primary)
				}
				.buttonStyle(.keyed(.primary))
				.accessibilityIdentifier("pane.extensionPopup.accept")
			}
			.padding(.top, 24)
		}
		.padding(24)
		.frame(width: 424, alignment: .leading)
		.surfaceBackground(cornerRadius: 29, shadow: Palette.dialogShadow, blur: 64, y: 24)
		.accessibilityElement(children: .contain)
		.accessibilityAddTraits(.isModal)
		.accessibilityLabel(prompt.title)
		.accessibilityIdentifier("pane.extensionInstall")
	}
}
