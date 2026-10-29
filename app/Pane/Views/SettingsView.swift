import PaneKit
import SwiftUI

// Settings, in Aside's layout (Paper: 06.1 Settings): a large title, then a
// muted label over each section's card. A card's rows each hold a title and
// a description, with their control at the trailing end: a pop-up button
// (the system's own menu, with up and down chevrons) or outline buttons.
struct SettingsView: View {
	let settings: AppSettings
	let lock: AppLock
	let updater: Updater
	// Changes the settings: BrowserController.changeSettings, which applies
	// a theme at once.
	let change: (_ edit: (inout AppSettings) -> Void) -> Void
	// Opens the App icon picker (BrowserController.openAppIconPicker).
	let chooseAppIcon: () -> Void

	// Pane's version (package.sh puts it in Info.plist; the bundle's own
	// versions are Chromium's).
	private static let version = Bundle.main.object(forInfoDictionaryKey: "PaneVersion") as? String ?? "…"

	var body: some View {
		ScrollView {
			VStack(alignment: .leading, spacing: 28) {
				Text("Settings")
					.font(.system(size: 22, weight: .medium))
					.foregroundStyle(Palette.accentForeground)
					.lineBox(28)

				VStack(alignment: .leading, spacing: 28) {
					SettingsGroup(title: "Appearance") {
						SettingsRow(title: "Theme", description: "Choose how Pane looks.") {
							PaneMenuButton(
								options: Theme.allCases.map { ($0.rawValue.capitalized, $0) },
								selection: settings.theme,
								label: "Theme",
								icon: Self.icon(for:),
								chevron: .upDown
							) { theme in
								change { $0.theme = theme }
							}
							.accessibilityIdentifier("pane.settings.theme")
						}

						// ADR 0089: the chosen icon; a click opens every sky.
						SettingsRow(title: "App icon", description: "Automatic follows the theme: Day when light, Night when dark.", last: true) {
							AppIconButton(icon: settings.appIcon, action: chooseAppIcon)
						}
					}

					SettingsGroup(title: "Memory & disk") {
						SettingsRow(title: "Sleep background tabs", description: "A tab in the background sleeps after this long, to free memory.") {
							PaneMenuButton(options: Self.tabSleepOptions, selection: settings.tabSleepAfterMinutes, label: "Sleep background tabs", chevron: .upDown) { minutes in
								change { $0.tabSleepAfterMinutes = minutes }
							}
							.accessibilityIdentifier("pane.settings.tabSleep")
						}

						SettingsRow(title: "Unload hidden profiles", description: "A profile out of view unloads after this long.") {
							PaneMenuButton(options: Self.profileUnloadOptions, selection: settings.profileUnloadAfterMinutes, label: "Unload hidden profiles", chevron: .upDown) { minutes in
								change { $0.profileUnloadAfterMinutes = minutes }
							}
							.accessibilityIdentifier("pane.settings.profileUnload")
						}

						SettingsRow(title: "Trim cache", description: "A profile that unloads trims its cache past this size.", last: true) {
							PaneMenuButton(options: Self.cacheBudgetOptions, selection: settings.cacheBudgetMB, label: "Trim cache", chevron: .upDown) { megabytes in
								change { $0.cacheBudgetMB = megabytes }
							}
							.accessibilityIdentifier("pane.settings.cacheBudget")
						}
					}

					SettingsGroup(title: "Security") {
						if lock.state.pin == nil {
							SettingsRow(title: "PIN", description: "Lock Pane with a PIN.", last: true) {
								Button("Set up PIN") { lock.show(.setup) }
									.buttonStyle(.pane(.outline, size: .small))
									.accessibilityIdentifier("pane.settings.setUpPIN")
							}
						} else {
							SettingsRow(title: "PIN", description: "Pane asks for it when it opens.", last: true) {
								Button("Change PIN") { lock.show(.change) }
									.buttonStyle(.pane(.outline, size: .small))
									.accessibilityIdentifier("pane.settings.changePIN")
								Button("Remove PIN") { lock.show(.remove) }
									.buttonStyle(.pane(.outline, size: .small))
									.accessibilityIdentifier("pane.settings.removePIN")
							}
						}
					}

					SettingsGroup(title: "Updates") {
						UpdateRow(updater: updater)

						SettingsRow(title: "Version", last: true) {
							Text(Self.version)
								.font(.system(size: 13).monospacedDigit())
								.foregroundStyle(Palette.mutedForeground)
						}
					}
				}
				.frame(maxWidth: 640, alignment: .leading)
			}
			.padding(.horizontal, 40)
			.padding(.vertical, 32)
			.frame(maxWidth: .infinity, alignment: .leading)
		}
		.background(Palette.card)
		.accessibilityIdentifier("pane.settings")
	}

	private static func icon(for theme: Theme) -> Lucide {
		switch theme {
		case .system: .monitor
		case .light: .sun
		case .dark: .moon
		}
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

// A section: its muted label 15 points in, over a card rounded 22 with a
// 1-point border, its rows inside.
private struct SettingsGroup<Content: View>: View {
	let title: String
	@ViewBuilder let content: Content

	var body: some View {
		VStack(alignment: .leading, spacing: 10) {
			Text(title)
				.font(.system(size: 13))
				.foregroundStyle(Palette.mutedForeground)
				.padding(.leading, 15)

			VStack(spacing: 0) {
				content
			}
			.background(Palette.card, in: Squircle(cornerRadius: 22))
			.overlay {
				Squircle(cornerRadius: 22)
					.strokeBorder(Palette.border, lineWidth: 1)
			}
			.accessibilityElement(children: .contain)
		}
	}
}

// A row, at least 56 points: the title at 13 and its description at 12,
// muted, then the control 10 points from the card's edge; a hairline under
// every row but the last.
private struct SettingsRow<Control: View>: View {
	let title: String
	var description: String?
	var last = false
	@ViewBuilder let control: Control

	var body: some View {
		HStack(spacing: 16) {
			VStack(alignment: .leading, spacing: 2) {
				Text(title)
					.font(.system(size: 13))
					.foregroundStyle(Palette.foreground)

				if let description {
					// A status that changes (the update check's) crossfades,
					// as the update pill's does.
					Text(description)
						.font(.system(size: 12))
						.foregroundStyle(Palette.mutedForeground)
						.contentTransition(.opacity)
						.animation(Motion.status, value: description)
				}
			}
			.frame(maxWidth: .infinity, alignment: .leading)

			HStack(spacing: 8) {
				control
			}
		}
		.padding(.leading, 15)
		.padding(.trailing, 10)
		.padding(.vertical, 12)
		.frame(minHeight: 56)
		.overlay(alignment: .bottom) {
			if !last {
				Hairline()
			}
		}
	}
}

// Updates: what the last check found, then Check for updates; or the
// available version, then its download.
private struct UpdateRow: View {
	let updater: Updater

	var body: some View {
		SettingsRow(title: "Updates", description: status) {
			if updater.available != nil {
				Button(downloadLabel) { updater.downloadUpdate() }
					.buttonStyle(.pane(.outline, size: .small))
					.disabled(updater.status == .downloading || updater.status == .downloaded)
					.accessibilityIdentifier("pane.settings.downloadUpdate")
			} else {
				Button(updater.status == .checking ? "Checking\u{2026}" : "Check for updates") {
					Task { await updater.check() }
				}
				.buttonStyle(.pane(.outline, size: .small))
				.disabled(updater.status == .checking)
				.accessibilityIdentifier("pane.settings.checkUpdates")
			}
		}
		.accessibilityElement(children: .contain)
		.accessibilityIdentifier(updater.available == nil && updater.lastCheckedAt != nil ? "pane.settings.upToDate" : "pane.settings.updates")
	}

	private var status: String {
		if let available = updater.available {
			return "Version \(available.version) is available."
		}

		if let checked = updater.lastCheckedAt {
			return "Pane is up to date \u{00B7} checked at \(checked.formatted(date: .omitted, time: .shortened))"
		}

		return "Pane checks for a new version on its own."
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
