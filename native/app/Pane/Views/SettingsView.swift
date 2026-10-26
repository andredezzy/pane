import PaneKit
import SwiftUI

// Settings, as the Electron app's page: px-10 py-8, a 480px column of
// sections 24px apart, each under a caps label and a hairline; the app's
// version at the bottom right.
struct SettingsView: View {
	let controller: BrowserController

	private var model: BrowserModel { controller.model }

	// Pane's version (package.sh puts it in Info.plist; the bundle's own
	// versions are Chromium's).
	private static let version = Bundle.main.object(forInfoDictionaryKey: "PaneVersion") as? String ?? "…"

	var body: some View {
		GeometryReader { geometry in
			ScrollView {
				VStack(alignment: .leading, spacing: 0) {
					VStack(alignment: .leading, spacing: 24) {
						VStack(alignment: .leading, spacing: 0) {
							Text("Settings")
								.font(.system(size: 14, weight: .medium))
								.foregroundStyle(Palette.accentForeground)
								.lineBox(20)
							Text("Manage your application preferences.")
								.font(.system(size: 12))
								.foregroundStyle(Palette.mutedForeground)
								.lineBox(16)
						}

						PaneSeparator()

						SettingsSection(title: "Appearance") {
							PresetRow(
								options: Theme.allCases,
								selection: model.settings.theme,
								title: { $0.rawValue.capitalized },
								select: controller.setTheme
							)
							.accessibilityElement(children: .contain)
							.accessibilityIdentifier("pane.settings.theme")
						}

						PaneSeparator()

						SettingsSection(title: "Extensions") {
							ExtensionSettings(controller: controller)
						}
					}
					.frame(maxWidth: 480, alignment: .leading)

					Spacer(minLength: 32)

					Text("Version \(Self.version)")
						.font(.system(size: 11).monospacedDigit())
						.lineBox(16.5)
						.foregroundStyle(Palette.mutedForeground)
						.frame(maxWidth: .infinity, alignment: .trailing)
				}
				.padding(.horizontal, 40)
				.padding(.vertical, 32)
				.frame(minHeight: geometry.size.height, alignment: .top)
			}
		}
		.background(Palette.card)
		.accessibilityIdentifier("pane.settings")
	}
}

// A caps label, then the section 12px below.
struct SettingsSection<Content: View>: View {
	let title: String
	@ViewBuilder let content: Content

	var body: some View {
		VStack(alignment: .leading, spacing: 12) {
			SectionLabel(title: title)
			content
		}
	}
}
