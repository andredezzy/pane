import SwiftUI

// A page that keeps crashing: Chrome's sad tab, drawn by the app.
struct SadTabView: View {
	let reload: () -> Void

	var body: some View {
		VStack(spacing: 12) {
			Image(systemName: "exclamationmark.triangle")
				.font(.system(size: 30, weight: .light))
				.foregroundStyle(.secondary)
			Text("This page crashed")
				.font(.system(size: 15, weight: .semibold))
			Text("Something went wrong while displaying this page.")
				.font(.system(size: 12))
				.foregroundStyle(.secondary)
			Button("Reload", action: reload)
				.keyboardShortcut(.defaultAction)
				.accessibilityIdentifier("pane.sadTab.reload")
		}
		.frame(maxWidth: .infinity, maxHeight: .infinity)
		.background(Color(nsColor: .windowBackgroundColor))
		.accessibilityElement(children: .contain)
		.accessibilityIdentifier("pane.sadTab")
	}
}
