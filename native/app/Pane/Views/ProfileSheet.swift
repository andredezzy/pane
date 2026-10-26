import PaneKit
import SwiftUI

// Create or edit a profile: name, color, and group.
struct ProfileSheet: View {
	let controller: BrowserController
	let profileID: String?
	let onDone: () -> Void

	@State private var name = ""
	@State private var color: ProfileColor = .blue
	@State private var group = ""

	private var model: BrowserModel { controller.model }
	private var trimmedName: String { name.trimmingCharacters(in: .whitespaces) }

	var body: some View {
		VStack(alignment: .leading, spacing: 16) {
			Text(profileID == nil ? "New profile" : "Edit profile")
				.font(.system(size: 15, weight: .semibold))

			VStack(alignment: .leading, spacing: 6) {
				Text("Name").font(.system(size: 12, weight: .medium))
				TextField("e.g. Work account", text: $name)
					.textFieldStyle(.roundedBorder)
					.accessibilityIdentifier("pane.profile.name")
			}

			VStack(alignment: .leading, spacing: 6) {
				Text("Color").font(.system(size: 12, weight: .medium))
				HStack(spacing: 8) {
					ForEach(ProfileColor.allCases, id: \.self) { swatch in
						Button { color = swatch } label: {
							Circle()
								.fill(swatch.color)
								.frame(width: 18, height: 18)
								.overlay {
									Circle().strokeBorder(Color.primary.opacity(swatch == color ? 0.6 : 0), lineWidth: 2)
								}
						}
						.buttonStyle(.plain)
						.accessibilityLabel(swatch.rawValue.capitalized)
						.accessibilityAddTraits(swatch == color ? .isSelected : [])
					}
				}
			}

			VStack(alignment: .leading, spacing: 6) {
				Text("Group").font(.system(size: 12, weight: .medium))
				TextField("Enter group name", text: $group)
					.textFieldStyle(.roundedBorder)
			}

			HStack {
				Spacer()
				Button("Cancel", role: .cancel, action: onDone)
					.keyboardShortcut(.cancelAction)
				Button(profileID == nil ? "Create" : "Save", action: save)
					.keyboardShortcut(.defaultAction)
					.disabled(trimmedName.isEmpty)
					.accessibilityIdentifier("pane.profile.save")
			}
		}
		.padding(20)
		.frame(width: 360)
		.onAppear(perform: load)
	}

	private func load() {
		guard let profile = profileID.flatMap(model.profile) else {
			return
		}

		name = profile.name
		color = profile.color
		group = profile.group ?? ""
	}

	private func save() {
		let groupValue = group.trimmingCharacters(in: .whitespaces)

		if let profileID {
			model.updateProfile(profileID) {
				$0.name = trimmedName
				$0.color = color
				$0.group = groupValue.isEmpty ? nil : groupValue
			}
		} else {
			let id = model.createProfile(name: trimmedName, color: color, group: groupValue.isEmpty ? nil : groupValue)
			model.expandedProfileIDs.insert(id)
		}

		onDone()
	}
}
