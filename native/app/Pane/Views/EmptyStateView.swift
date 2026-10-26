import SwiftUI

// What the content area shows with no tab selected.
struct EmptyStateView: View {
	let hasProfiles: Bool

	var body: some View {
		VStack(spacing: 6) {
			Text(hasProfiles ? "No tab open" : "No profiles yet")
				.font(.system(size: 13, weight: .medium))
			Text(hasProfiles ? "Pick a tab in the sidebar, or press ⌘T for a new one." : "Create a profile with New in the sidebar.")
				.font(.system(size: 12))
				.foregroundStyle(.secondary)
		}
		.frame(maxWidth: .infinity, maxHeight: .infinity)
		.background(Color(nsColor: .textBackgroundColor))
		.accessibilityIdentifier("pane.empty")
	}
}
