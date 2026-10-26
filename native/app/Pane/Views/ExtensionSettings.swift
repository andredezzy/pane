import PaneKit
import SwiftUI

// Settings › Extensions: a profile's extensions, installed from the Chrome
// Web Store by URL or ID, each enabled, pinned, or removed.
struct ExtensionSettings: View {
	let controller: BrowserController

	@State private var profileID: String?
	@State private var input = ""
	@State private var removing: ExtensionInfo?

	private var model: BrowserModel { controller.model }
	private var selectedProfileID: String? { profileID ?? model.activeProfileID ?? model.profiles.first?.id }
	private var extensions: [ExtensionInfo] { selectedProfileID.flatMap { model.extensions[$0] } ?? [] }
	private var webStoreID: String? { ExtensionInfo.webStoreID(from: input.trimmingCharacters(in: .whitespaces)) }

	var body: some View {
		VStack(alignment: .leading, spacing: 12) {
			if model.profiles.isEmpty {
				Text("Create a profile to install extensions.")
					.foregroundStyle(.secondary)
			} else {
				Picker("Profile", selection: Binding(
					get: { selectedProfileID ?? "" },
					set: { profileID = $0 }
				)) {
					ForEach(model.profiles) { profile in
						Text(profile.name).tag(profile.id)
					}
				}
				.frame(maxWidth: 280)
				.accessibilityIdentifier("pane.settings.extensions.profile")

				HStack {
					TextField("Chrome Web Store URL or extension ID", text: $input)
						.textFieldStyle(.roundedBorder)
						.frame(maxWidth: 380)
						.onSubmit(install)
						.accessibilityIdentifier("pane.settings.extensions.input")
					Button("Install", action: install)
						.disabled(webStoreID == nil)
					Button("Update All") {
						selectedProfileID.map { controller.updateExtensions(inProfile: $0) }
					}
					.disabled(extensions.isEmpty)
				}

				if extensions.isEmpty {
					Text("No extensions in this profile.")
						.font(.system(size: 12))
						.foregroundStyle(.secondary)
				}

				ForEach(extensions) { item in
					ExtensionRow(
						item: item,
						setEnabled: { enabled in
							selectedProfileID.map { controller.setExtension(item.id, enabled: enabled, inProfile: $0) }
						},
						setPinned: { pinned in
							selectedProfileID.map { controller.setExtension(item.id, pinned: pinned, inProfile: $0) }
						},
						remove: { removing = item }
					)
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
		.alert(
			"Remove “\(removing?.name ?? "")”?",
			isPresented: Binding(get: { removing != nil }, set: { if !$0 { removing = nil } })
		) {
			Button("Remove", role: .destructive) {
				if let removing, let profileID = selectedProfileID {
					controller.uninstallExtension(removing.id, inProfile: profileID)
				}

				removing = nil
			}
			Button("Cancel", role: .cancel) { removing = nil }
		}
		.accessibilityElement(children: .contain)
		.accessibilityIdentifier("pane.settings.extensions")
	}

	private func install() {
		guard let id = webStoreID, let profileID = selectedProfileID else {
			return
		}

		controller.installExtension(id, inProfile: profileID)
		input = ""
	}
}

private struct ExtensionRow: View {
	let item: ExtensionInfo
	let setEnabled: @MainActor (Bool) -> Void
	let setPinned: @MainActor (Bool) -> Void
	let remove: () -> Void

	var body: some View {
		HStack(spacing: 10) {
			Group {
				if let icon = item.icon {
					Image(nsImage: icon).resizable()
				} else {
					Image(systemName: "puzzlepiece.extension").foregroundStyle(.secondary)
				}
			}
			.frame(width: 20, height: 20)

			VStack(alignment: .leading, spacing: 1) {
				Text(item.name).font(.system(size: 13, weight: .medium))
				Text(item.version).font(.system(size: 11)).foregroundStyle(.secondary)
			}

			Spacer()

			if item.hasAction {
				Toggle("Pinned", isOn: Binding(get: { item.pinned }, set: setPinned))
					.toggleStyle(.checkbox)
					.disabled(!item.enabled)
			}

			Toggle("Enabled", isOn: Binding(get: { item.enabled }, set: setEnabled))
				.toggleStyle(.switch)
				.labelsHidden()
				.accessibilityLabel("Enabled")

			Button("Remove", action: remove)
		}
		.padding(10)
		.background(Color.primary.opacity(0.04), in: RoundedRectangle(cornerRadius: 8, style: .continuous))
		.frame(maxWidth: 560)
		.accessibilityElement(children: .contain)
		.accessibilityIdentifier("pane.settings.extension.\(item.id)")
	}
}
