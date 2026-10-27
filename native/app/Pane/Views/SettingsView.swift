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
			ScrollViewReader { proxy in
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

							SettingsSection(title: "Memory & disk") {
								VStack(alignment: .leading, spacing: 16) {
									LabeledPresetRow(
										label: "Sleep background tabs after",
										options: Self.tabSleepOptions,
										selection: model.settings.tabSleepAfterMinutes
									) { model.settings.tabSleepAfterMinutes = $0 }
										.accessibilityIdentifier("pane.settings.tabSleep")
									LabeledPresetRow(
										label: "Unload hidden profiles after",
										options: Self.profileUnloadOptions,
										selection: model.settings.profileUnloadAfterMinutes
									) { model.settings.profileUnloadAfterMinutes = $0 }
										.accessibilityIdentifier("pane.settings.profileUnload")
									LabeledPresetRow(
										label: "Trim cache over",
										options: Self.cacheBudgetOptions,
										selection: model.settings.cacheBudgetMB
									) { model.settings.cacheBudgetMB = $0 }
										.accessibilityIdentifier("pane.settings.cacheBudget")
								}
							}

							PaneSeparator()

							SettingsSection(title: "Extensions") {
								ExtensionSettings(controller: controller)
							}
							.id("pane.settings.extensions")

							PaneSeparator()

							SettingsSection(title: "Security") {
								if controller.lock.state.pin == nil {
									Button("Set up PIN") { controller.lock.show(.setup) }
										.buttonStyle(.pane(.outline))
										.accessibilityIdentifier("pane.settings.setUpPIN")
								} else {
									HStack(spacing: 8) {
										Button("Change PIN") { controller.lock.show(.change) }
											.buttonStyle(.pane(.outline))
											.accessibilityIdentifier("pane.settings.changePIN")
										Button("Remove PIN") { controller.lock.show(.remove) }
											.buttonStyle(.pane(.outline))
											.accessibilityIdentifier("pane.settings.removePIN")
									}
								}
							}

							PaneSeparator()

							SettingsSection(title: "Updates") {
								UpdateSettings(updater: controller.updater)
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
				// The extensions menu's Manage items open Settings at Extensions,
				// once the section is laid out.
				.onChange(of: controller.state.settingsAnchor, initial: true) { _, anchor in
					guard let anchor else {
						return
					}

					DispatchQueue.main.async {
						proxy.scrollTo(anchor, anchor: .top)
						controller.state.settingsAnchor = nil
					}
				}
			}
		}
		.background(Palette.card)
		.accessibilityIdentifier("pane.settings")
	}
}

// Presets rather than a free field (resource-settings.tsx): the scheduler
// ticks once a minute, so finer values would be false precision; Off is the
// kill switch.
extension SettingsView {
	static let tabSleepOptions: [(String, Int?)] = [("5 min", 5), ("15 min", 15), ("1 hour", 60), ("Off", nil)]
	static let profileUnloadOptions: [(String, Int?)] = [("15 min", 15), ("30 min", 30), ("2 hours", 120), ("Off", nil)]
	// Megabytes, per profile: no cache is shared.
	static let cacheBudgetOptions: [(String, Int?)] = [("100 MB", 100), ("300 MB", 300), ("1 GB", 1024), ("Off", nil)]
}

// A medium 11px label, the presets 8px below.
private struct LabeledPresetRow: View {
	let label: String
	let options: [(String, Int?)]
	let selection: Int?
	let select: (Int?) -> Void

	var body: some View {
		VStack(alignment: .leading, spacing: 8) {
			Text(label)
				.font(.system(size: 11, weight: .medium))
				.foregroundStyle(Palette.foreground)
				.lineBox(16.5)

			PresetRow(
				options: options.map(\.1),
				selection: selection,
				title: { value in options.first { $0.1 == value }?.0 ?? "" },
				select: select
			)
			.accessibilityElement(children: .contain)
		}
	}
}

// The Electron app's Updates section: Check for updates and when it last
// checked, or the available version and its download.
private struct UpdateSettings: View {
	let updater: Updater

	var body: some View {
		VStack(alignment: .leading, spacing: 12) {
			if let available = updater.available {
				VStack(alignment: .leading, spacing: 8) {
					Text("Version \(available.version) is available")
						.font(.system(size: 12))
						.foregroundStyle(Palette.mutedForeground)
						.lineBox(16)

					Button(downloadLabel) { updater.downloadUpdate() }
						.buttonStyle(.pane(.outline))
						.disabled(updater.status == .downloading || updater.status == .downloaded)
						.accessibilityIdentifier("pane.settings.downloadUpdate")
				}
			} else {
				Button(updater.status == .checking ? "Checking\u{2026}" : "Check for updates") {
					Task { await updater.check() }
				}
				.buttonStyle(.pane(.outline))
				.disabled(updater.status == .checking)
				.accessibilityIdentifier("pane.settings.checkUpdates")

				if let checked = updater.lastCheckedAt {
					Text("Pane is up to date \u{00B7} checked at \(checked.formatted(date: .omitted, time: .shortened))")
						.font(.system(size: 12))
						.foregroundStyle(Palette.mutedForeground)
						.lineBox(16)
						.accessibilityIdentifier("pane.settings.upToDate")
				}
			}
		}
	}

	private var downloadLabel: String {
		switch updater.status {
		case .downloading:
			let percent = Int(((updater.downloadProgress ?? 0) * 100).rounded())
			let eta = updater.downloadETA.map { " \u{00B7} \(Updates.formatETA(seconds: $0))" } ?? ""
			return "Downloading\u{2026} \(percent)%\(eta)"
		case .downloaded:
			return "Downloaded, quit to install"
		default:
			return "Download update"
		}
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
