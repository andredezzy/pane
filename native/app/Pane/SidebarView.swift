import SwiftUI

// Placeholder sidebar until profiles and tabs arrive (plan, Phase 2).
struct SidebarView: View {
	var body: some View {
		VStack(alignment: .leading, spacing: 8) {
			Text("Pane")
				.font(.headline)
			Text("Profiles and tabs")
				.font(.subheadline)
				.foregroundStyle(.secondary)
			Spacer()
		}
		.padding(.horizontal, 16)
		.padding(.top, 44)
		.frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
		.accessibilityElement(children: .contain)
		.accessibilityIdentifier("pane.sidebar.list")
		.accessibilityLabel("Sidebar")
	}
}
