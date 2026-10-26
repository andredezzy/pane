import SwiftUI

// The Electron app's Dialog: 384px wide, p-6, rounded-lg, a border and
// shadow-lg; the title (18px semibold), the message, then Cancel and the
// destructive confirm button. A close X sits at the top right.
struct ConfirmDialogView: View {
	let dialog: WindowState.ConfirmDialog
	let dismiss: () -> Void

	@State private var closeHovered = false

	var body: some View {
		VStack(alignment: .leading, spacing: 16) {
			VStack(alignment: .leading, spacing: 6) {
				Text(dialog.title)
					.font(.system(size: 18, weight: .semibold))
					.tracking(-0.45)
					.foregroundStyle(Palette.foreground)
					.lineBox(18)
				Text(dialog.message)
					.font(.system(size: 14))
					.foregroundStyle(Palette.mutedForeground)
					// text-sm: 20px lines.
					.lineSpacing(3)
					.frame(minHeight: 20)
					.fixedSize(horizontal: false, vertical: true)
			}
			.padding(.trailing, 24)

			HStack(spacing: 8) {
				Spacer()
				Button("Cancel", action: dismiss)
					.buttonStyle(.pane(.outline))
					.keyboardShortcut(.cancelAction)
				Button(dialog.confirmTitle) {
					dismiss()
					dialog.confirm()
				}
				.buttonStyle(.pane(.destructive))
				.keyboardShortcut(.defaultAction)
				.accessibilityIdentifier("pane.dialog.confirm")
			}
		}
		.padding(24)
		.frame(width: 384, alignment: .leading)
		.surfaceBackground(cornerRadius: 8, shadow: .black.opacity(0.1), blur: 15, y: 10)
		.overlay(alignment: .topTrailing) {
			Button(action: dismiss) {
				LucideIcon(.x, size: 16)
			}
			.buttonStyle(.plain)
			.foregroundStyle(Palette.foreground)
			.opacity(closeHovered ? 1 : 0.7)
			.onHover { closeHovered = $0 }
			.padding(16)
			.accessibilityLabel("Close")
		}
		.onExitCommand(perform: dismiss)
		.accessibilityElement(children: .contain)
		.accessibilityAddTraits(.isModal)
		.accessibilityIdentifier("pane.dialog")
	}
}
