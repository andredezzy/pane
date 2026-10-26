import PaneKit
import SwiftUI

// Create or edit a profile, as the Electron app's sheet: a 360px panel inset
// 8px from the window's top, right, and bottom, with the title and a close X,
// the form (name, group, color), and Cancel / Create.
struct ProfileSheet: View {
	let controller: BrowserController
	let profileID: String?
	let onDone: () -> Void

	@State private var name = ""
	@State private var color: ProfileColor = .blue
	@State private var group = ""
	@State private var nameError: String?

	private var model: BrowserModel { controller.model }
	private var trimmedName: String { name.trimmingCharacters(in: .whitespaces) }

	var body: some View {
		VStack(spacing: 0) {
			header
			hairline

			ScrollView {
				VStack(alignment: .leading, spacing: 16) {
					VStack(alignment: .leading, spacing: 0) {
						FormLabel(title: "Name")
						PaneTextField(placeholder: "e.g. Work account", text: $name, focusesOnAppear: true, onSubmit: save)
							.accessibilityIdentifier("pane.profile.name")

						if let nameError {
							Text(nameError)
								.font(.system(size: 12.8, weight: .medium))
								.foregroundStyle(Palette.destructive)
						}
					}

					VStack(alignment: .leading, spacing: 0) {
						FormLabel(title: "Group", note: "(optional)")
						PaneTextField(placeholder: "Enter group name", text: $group, onSubmit: save)
					}

					VStack(alignment: .leading, spacing: 0) {
						FormLabel(title: "Color")
						SwatchPicker(selection: $color)
					}
				}
				.padding(20)
				.frame(maxWidth: .infinity, alignment: .leading)
			}

			hairline

			HStack(spacing: 8) {
				Spacer()
				Button("Cancel", action: onDone)
					.buttonStyle(.pane(.outline))
					.keyboardShortcut(.cancelAction)
				Button(profileID == nil ? "Create" : "Save", action: save)
					.buttonStyle(.pane(.primary))
					.keyboardShortcut(.defaultAction)
					.accessibilityIdentifier("pane.profile.save")
			}
			.padding(.horizontal, 20)
			.padding(.vertical, 16)
		}
		// Clipped before the shadow goes on, which would clip it too.
		.clipShape(RoundedRectangle(cornerRadius: 10, style: .circular))
		.surfaceBackground(cornerRadius: 10, shadow: .black.opacity(0.5), blur: 40, y: 8)
		.onAppear(perform: load)
		.onChange(of: name) { nameError = nil }
		.onExitCommand(perform: onDone)
		.accessibilityElement(children: .contain)
		.accessibilityIdentifier("pane.profileSheet")
	}

	private var header: some View {
		HStack {
			Text(profileID == nil ? "New profile" : "Edit profile")
				.font(.system(size: 14, weight: .medium))
				.foregroundStyle(Palette.accentForeground)
				.lineBox(20)

			Spacer()

			CloseButton(action: onDone)
		}
		.frame(height: 24)
		.padding(.horizontal, 20)
		.padding(.vertical, 16)
	}

	// The Electron app's header and footer rules: white 5%, which only shows
	// on dark.
	private var hairline: some View {
		Rectangle()
			.fill(Color.white.opacity(0.05))
			.frame(height: 1)
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
		guard !trimmedName.isEmpty else {
			nameError = "Profile name is required"
			return
		}

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

// 24px, rounded 4: a 16px X, muted until hovered.
struct CloseButton: View {
	let action: () -> Void

	@State private var isHovered = false
	@Environment(\.accessibilityReduceMotion) private var reduceMotion

	var body: some View {
		Button(action: action) {
			LucideIcon(.x, size: 16)
				.frame(width: 24, height: 24)
				.contentShape(Rectangle())
		}
		.buttonStyle(.plain)
		.foregroundStyle(isHovered ? Palette.foreground : Palette.mutedForeground)
		.onHover { hovering in
			withAnimation(reduceMotion ? nil : .easeOut(duration: 0.15)) { isHovered = hovering }
		}
		.accessibilityLabel("Close")
	}
}

// 24px swatches, rounded-md, 6px apart; the selected one is ringed with the
// background then its color.
private struct SwatchPicker: View {
	@Binding var selection: ProfileColor

	var body: some View {
		HStack(spacing: 6) {
			ForEach(ProfileColor.allCases, id: \.self) { swatch in
				let isSelected = swatch == selection

				Button { selection = swatch } label: {
					RoundedRectangle(cornerRadius: 6, style: .circular)
						.fill(swatch.color)
						.frame(width: 24, height: 24)
						.background {
							if isSelected {
								RoundedRectangle(cornerRadius: 9, style: .circular)
									.fill(swatch.color)
									.padding(-3)
								RoundedRectangle(cornerRadius: 8, style: .circular)
									.fill(Palette.background)
									.padding(-2)
							}
						}
				}
				.buttonStyle(.plain)
				.accessibilityLabel(swatch.rawValue.capitalized)
				.accessibilityAddTraits(isSelected ? .isSelected : [])
			}
		}
	}
}
