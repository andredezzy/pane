import SwiftUI

// A page that keeps crashing: Chrome's sad tab, drawn by the app in the
// content panel's style (the page title size and muted line of the empty
// state, an outline button).
struct SadTabView: View {
	let reload: () -> Void

	var body: some View {
		VStack(spacing: 12) {
			VStack(spacing: 4) {
				Text("This page crashed")
					.font(.system(size: 14, weight: .medium))
					.foregroundStyle(Palette.accentForeground)
					.lineBox(20)
				Text("Something went wrong while displaying this page.")
					.font(.system(size: 12))
					.foregroundStyle(Palette.mutedForeground)
					.lineBox(16)
			}

			Button("Reload", action: reload)
				.buttonStyle(.pane(.outline))
				.keyboardShortcut(.defaultAction)
				.accessibilityIdentifier("pane.sadTab.reload")
		}
		.frame(maxWidth: .infinity, maxHeight: .infinity)
		.background(Palette.card)
		.accessibilityElement(children: .contain)
		.accessibilityIdentifier("pane.sadTab")
	}
}
