import PaneKit
import SwiftUI

// Settings › Extensions, as the Electron app's section: install from the
// Chrome Web Store by URL or ID, then a card per extension with Uninstall
// (after a confirmation). Extensions here are per profile, so a profile
// select leads, and each card also pins and enables its extension.
struct ExtensionSettings: View {
	let controller: BrowserController

	@State private var profileID: String?
	@State private var input = ""
	@State private var inputError: String?

	private var model: BrowserModel { controller.model }
	private var selectedProfileID: String? { profileID ?? model.activeProfileID ?? model.profiles.first?.id }
	private var selectedProfile: BrowserProfile? { selectedProfileID.flatMap(model.profile) }
	private var extensions: [ExtensionInfo] { selectedProfileID.flatMap { model.extensions[$0] } ?? [] }

	var body: some View {
		VStack(alignment: .leading, spacing: 0) {
			if model.profiles.isEmpty {
				Text("Create a profile to install extensions.")
					.font(.system(size: 12))
					.foregroundStyle(Palette.mutedForeground)
			} else {
				HStack(spacing: 8) {
					ProfileSelect(
						profiles: model.profiles,
						selection: selectedProfile,
						select: { profileID = $0 }
					)

					Spacer()

					Button("Update all") {
						selectedProfileID.map { controller.updateExtensions(inProfile: $0) }
					}
					.buttonStyle(.pane(.outline, size: .small))
					.disabled(extensions.isEmpty)
				}

				VStack(alignment: .leading, spacing: 0) {
					FormLabel(title: "Install from Chrome Web Store")

					HStack(spacing: 8) {
						PaneTextField(placeholder: "Paste extension URL or ID", text: $input, onSubmit: install)
							.accessibilityIdentifier("pane.settings.extensions.input")
						Button("Install", action: install)
							.buttonStyle(.pane(.primary))
					}

					if let inputError {
						Text(inputError)
							.font(.system(size: 12.8, weight: .medium))
							.foregroundStyle(Palette.destructive)
					}
				}
				.padding(.top, 16)

				if extensions.isEmpty {
					Text("No extensions installed")
						.font(.system(size: 12))
						.foregroundStyle(Palette.mutedForeground)
						.lineBox(18)
						.padding(.top, 16)
				} else {
					VStack(spacing: 6) {
						ForEach(extensions) { item in
							ExtensionCard(
								item: item,
								setEnabled: { enabled in
									selectedProfileID.map { controller.setExtension(item.id, enabled: enabled, inProfile: $0) }
								},
								setPinned: { pinned in
									selectedProfileID.map { controller.setExtension(item.id, pinned: pinned, inProfile: $0) }
								},
								uninstall: { confirmUninstall(item) }
							)
						}
					}
					.padding(.top, 16)
				}
			}
		}
		.onAppear {
			selectedProfileID.map(controller.refreshExtensions)
		}
		.onChange(of: profileID) { _, id in
			if let id {
				controller.openContextIfNeeded(profileID: id)
			}
		}
		.onChange(of: input) { inputError = nil }
		.accessibilityElement(children: .contain)
		.accessibilityIdentifier("pane.settings.extensions")
	}

	private func install() {
		guard let id = ExtensionInfo.webStoreID(from: input.trimmingCharacters(in: .whitespaces)) else {
			inputError = input.isEmpty
				? "Enter a Chrome Web Store URL or extension ID"
				: "Enter a valid Chrome Web Store URL or extension ID"
			return
		}

		guard let profileID = selectedProfileID else {
			return
		}

		controller.installExtension(id, inProfile: profileID)
		input = ""
	}

	private func confirmUninstall(_ item: ExtensionInfo) {
		guard let profileID = selectedProfileID else {
			return
		}

		let profileName = selectedProfile?.name ?? "this profile"

		controller.state.dialog = WindowState.ConfirmDialog(
			title: "Uninstall \(item.name)?",
			message: "This will remove the extension from \u{201C}\(profileName)\u{201D}.",
			confirmTitle: "Uninstall",
			confirm: { [controller] in controller.uninstallExtension(item.id, inProfile: profileID) }
		)
	}
}

// A select trigger (h-9, rounded-md, a 1px input border): the profile whose
// extensions show, with its color.
private struct ProfileSelect: View {
	let profiles: [BrowserProfile]
	let selection: BrowserProfile?
	let select: (String) -> Void

	var body: some View {
		Menu {
			ForEach(profiles) { profile in
				Button(profile.name) { select(profile.id) }
			}
		} label: {
			HStack(spacing: 6) {
				if let selection {
					RoundedRectangle(cornerRadius: 2, style: .circular)
						.fill(selection.color.color)
						.frame(width: 4, height: 14)
				}
				Text(selection?.name ?? "Profile")
					.font(.system(size: 14))
					.foregroundStyle(Palette.foreground)
				Image(systemName: "chevron.down")
					.font(.system(size: 10, weight: .semibold))
					.foregroundStyle(Palette.mutedForeground)
			}
			.padding(.horizontal, 12)
			.frame(height: 36)
			.background(Palette.background, in: RoundedRectangle(cornerRadius: 6, style: .circular))
			.overlay {
				RoundedRectangle(cornerRadius: 6, style: .circular)
					.strokeBorder(Palette.border, lineWidth: 1)
			}
		}
		.menuStyle(.button)
		.buttonStyle(.plain)
		.menuIndicator(.hidden)
		.fixedSize()
		.accessibilityLabel("Profile")
		.accessibilityIdentifier("pane.settings.extensions.profile")
	}
}

// rounded-lg, border at 50%, card at 30%, p-3: the 32px icon, name and
// version, the description; Pinned and Enabled under it; Uninstall at the top
// right (ghost, small, red).
private struct ExtensionCard: View {
	let item: ExtensionInfo
	let setEnabled: @MainActor (Bool) -> Void
	let setPinned: @MainActor (Bool) -> Void
	let uninstall: () -> Void

	var body: some View {
		HStack(alignment: .top, spacing: 12) {
			Group {
				if let icon = item.icon {
					Image(nsImage: icon)
						.resizable()
						.interpolation(.high)
				} else {
					Palette.secondary
				}
			}
			.frame(width: 32, height: 32)
			.clipShape(RoundedRectangle(cornerRadius: 6, style: .circular))

			VStack(alignment: .leading, spacing: 2) {
				HStack(alignment: .firstTextBaseline, spacing: 6) {
					Text(item.name)
						.font(.system(size: 13, weight: .medium))
						.foregroundStyle(Palette.accentForeground)
						.lineLimit(1)
						.lineBox(19.5)
					Text("v\(item.version)")
						.font(.system(size: 11))
						.foregroundStyle(Palette.mutedForeground)
				}

				if !item.summary.isEmpty {
					Text(item.summary)
						.font(.system(size: 12))
						.foregroundStyle(Palette.mutedForeground)
						.lineLimit(1)
						.truncationMode(.tail)
						.lineBox(18)
				}

				HStack(spacing: 16) {
					if item.hasAction {
						Toggle(isOn: Binding(get: { item.pinned }, set: setPinned)) {
							Text("Pinned")
						}
						.disabled(!item.enabled)
					}

					Toggle(isOn: Binding(get: { item.enabled }, set: setEnabled)) {
						Text("Enabled")
					}
				}
				.toggleStyle(.paneSwitch)
				.font(.system(size: 11, weight: .medium))
				.foregroundStyle(Palette.mutedForeground)
				.padding(.top, 8)
			}
			.frame(maxWidth: .infinity, alignment: .leading)

			Button("Uninstall", action: uninstall)
				.buttonStyle(.pane(.ghost, size: .small, foreground: Palette.destructive))
		}
		.padding(12)
		.background(Palette.card.opacity(0.3), in: RoundedRectangle(cornerRadius: 8, style: .circular))
		.overlay {
			RoundedRectangle(cornerRadius: 8, style: .circular)
				.strokeBorder(Palette.border.opacity(0.5), lineWidth: 1)
		}
		.accessibilityElement(children: .contain)
		.accessibilityIdentifier("pane.settings.extension.\(item.id)")
	}
}
