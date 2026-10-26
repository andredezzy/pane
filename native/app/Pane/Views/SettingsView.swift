import PaneKit
import SwiftUI

struct SettingsView: View {
	let controller: BrowserController

	private var model: BrowserModel { controller.model }

	var body: some View {
		ScrollView {
			VStack(alignment: .leading, spacing: 24) {
				Text("Settings")
					.font(.system(size: 18, weight: .semibold))

				SettingsSection(title: "Appearance") {
					Picker("Theme", selection: Binding(
						get: { model.settings.theme },
						set: { controller.setTheme($0) }
					)) {
						Text("System").tag(Theme.system)
						Text("Light").tag(Theme.light)
						Text("Dark").tag(Theme.dark)
					}
					.pickerStyle(.segmented)
					.frame(maxWidth: 280)
					.accessibilityIdentifier("pane.settings.theme")
				}
			}
			.padding(28)
			.frame(maxWidth: .infinity, alignment: .leading)
		}
		.background(Color(nsColor: .textBackgroundColor))
		.accessibilityIdentifier("pane.settings")
	}
}

struct SettingsSection<Content: View>: View {
	let title: String
	@ViewBuilder let content: Content

	var body: some View {
		VStack(alignment: .leading, spacing: 10) {
			Text(title)
				.font(.system(size: 13, weight: .semibold))
			content
		}
	}
}
