import PaneKit
import SwiftUI

// The Extensions page (Paper: 08 · Extensions), Pane's chrome://extensions,
// as Chrome and Aside have theirs: a profile's extensions as cards, two to a
// row, found by name or description, each with its options, removal, pin,
// and switch. Installing by Web Store URL or ID follows the cards.
// Extensions are per profile, so a profile select leads, on the active one.
struct ExtensionsPage: View {
	let controller: BrowserController

	@State private var profileID: String?
	@State private var query = ""
	@State private var input = ""
	@State private var inputError: String?

	private var model: BrowserModel { controller.model }
	private var selectedProfileID: String? { profileID ?? model.activeProfileID ?? model.profiles.first?.id }
	private var selectedProfile: BrowserProfile? { selectedProfileID.flatMap(model.profile) }
	private var extensions: [ExtensionInfo] { selectedProfileID.flatMap { model.extensions[$0] } ?? [] }

	private var shown: [ExtensionInfo] {
		let query = query.trimmingCharacters(in: .whitespaces)

		guard !query.isEmpty else {
			return extensions
		}

		return extensions.filter { $0.name.localizedCaseInsensitiveContains(query) || $0.summary.localizedCaseInsensitiveContains(query) }
	}

	var body: some View {
		ScrollViewReader { proxy in
			ScrollView {
				VStack(alignment: .leading, spacing: 20) {
					header

					if model.profiles.isEmpty {
						Text("Create a profile to install extensions.")
							.font(.system(size: 12))
							.foregroundStyle(Palette.mutedForeground)
					} else {
						SearchField(text: $query)

						VStack(alignment: .leading, spacing: 10) {
							// The count sits on the label's line, set as the label is.
							HStack(spacing: 6) {
								SectionLabel(title: query.isEmpty ? "All extensions" : "Results")
								Text("\(shown.count)")
									.font(.system(size: 10))
									.foregroundStyle(Palette.mutedForeground)
									.offset(y: 2.5)
									.lineBox(24)
							}

							if extensions.isEmpty {
								empty
							} else {
								cards
							}
						}

						PaneSeparator()

						installSection
					}
				}
				.padding(.horizontal, 40)
				.padding(.vertical, 32)
				.frame(maxWidth: .infinity, alignment: .leading)
			}
			// Manage Extension opens the page at the extension's card, once
			// the cards are laid out.
			.onChange(of: controller.state.extensionsAnchor, initial: true) { _, anchor in
				guard let anchor else {
					return
				}

				DispatchQueue.main.async {
					proxy.scrollTo(anchor, anchor: .top)
					controller.state.extensionsAnchor = nil
				}
			}
		}
		.background(Palette.card)
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
		.accessibilityIdentifier("pane.extensions")
	}

	// The puzzle, the title and what the page holds; the profile, and Update
	// all, at the trailing edge.
	private var header: some View {
		HStack(spacing: 12) {
			Image(nsImage: Glyph.puzzle.image(strokeWidth: 1.6, size: 22))
				.renderingMode(.template)
				.foregroundStyle(Palette.accentForeground)

			VStack(alignment: .leading, spacing: 0) {
				Text("Extensions")
					.font(.system(size: 14, weight: .medium))
					.foregroundStyle(Palette.accentForeground)
					.lineBox(20)
				Text("Each profile keeps its own extensions.")
					.font(.system(size: 12))
					.foregroundStyle(Palette.mutedForeground)
					.lineBox(16)
			}

			Spacer()

			if !model.profiles.isEmpty {
				ProfileSelect(profiles: model.profiles, selection: selectedProfile) { profileID = $0 }

				Button("Update all") {
					selectedProfileID.map { controller.updateExtensions(inProfile: $0) }
				}
				.buttonStyle(.pane(.outline, size: .small))
				.disabled(extensions.isEmpty)
			}
		}
	}

	// Two to a row; a row is as tall as its taller card. Rows carry their
	// extensions, not indexes into the list: a removal shrinks the list
	// before SwiftUI drops the last row, and an index would outrun it.
	private var cards: some View {
		let rows = stride(from: 0, to: shown.count, by: 2).map { Array(shown[$0 ..< min($0 + 2, shown.count)]) }

		return Grid(horizontalSpacing: 12, verticalSpacing: 12) {
			ForEach(rows, id: \.first?.id) { row in
				GridRow {
					ForEach(row) { item in
						card(item)
					}

					if row.count == 1 {
						Color.clear
							.gridCellUnsizedAxes(.vertical)
					}
				}
			}
		}
	}

	private func card(_ item: ExtensionInfo) -> some View {
		ExtensionCard(
			item: item,
			setEnabled: { enabled in
				selectedProfileID.map { controller.setExtension(item.id, enabled: enabled, inProfile: $0) }
			},
			setPinned: { pinned in
				selectedProfileID.map { controller.setExtension(item.id, pinned: pinned, inProfile: $0) }
			},
			options: { controller.openExtensionOptions(item, inProfile: selectedProfileID) },
			remove: {
				selectedProfileID.map { controller.confirmUninstall(item, inProfile: $0) }
			}
		)
		// Where Manage Extension scrolls to.
		.id(ExtensionCard.identifier(item.id))
	}

	private var empty: some View {
		VStack(spacing: 6) {
			Text("No extensions in \(selectedProfile?.name ?? "this profile")")
				.font(.system(size: 13, weight: .medium))
				.foregroundStyle(Palette.accentForeground)
			Text("Add them from the Chrome Web Store, or by their URL or ID below.")
				.font(.system(size: 12))
				.foregroundStyle(Palette.mutedForeground)
			Button("Visit Chrome Web Store") { controller.visitWebStore(inProfile: selectedProfileID) }
				.buttonStyle(.pane(.outline, size: .small))
				.padding(.top, 10)
		}
		.frame(maxWidth: .infinity)
		.padding(.vertical, 48)
		.overlay {
			Squircle(cornerRadius: 15)
				.strokeBorder(Palette.border, style: StrokeStyle(lineWidth: 1, dash: [4, 3]))
		}
	}

	private var installSection: some View {
		VStack(alignment: .leading, spacing: 8) {
			FormLabel(title: "Install from Chrome Web Store")

			HStack(spacing: 8) {
				PaneTextField(placeholder: "Paste extension URL or ID", text: $input, onSubmit: install)
					.accessibilityIdentifier("pane.extensions.input")
				Button("Install", action: install)
					.buttonStyle(.pane(.primary))
			}

			if let inputError {
				Text(inputError)
					.font(.system(size: 12.8, weight: .medium))
					.foregroundStyle(Palette.destructive)
			}

			HStack(spacing: 4) {
				Text("Or find one on the")
					.foregroundStyle(Palette.mutedForeground)
				Button("Chrome Web Store \u{2197}") { controller.visitWebStore(inProfile: selectedProfileID) }
					.buttonStyle(.plain)
					.foregroundStyle(Color(nsColor: .linkColor))
			}
			.font(.system(size: 12))
		}
		.frame(maxWidth: 560, alignment: .leading)
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
}

// A text field with a magnifying glass: what the cards are narrowed to.
private struct SearchField: View {
	@Binding var text: String

	@FocusState private var focused: Bool

	var body: some View {
		HStack(spacing: 8) {
			Image(systemName: "magnifyingglass")
				.font(.system(size: 13))
				.foregroundStyle(Palette.mutedForeground)

			TextField("", text: $text, prompt: Text("Search extensions").foregroundStyle(Palette.mutedForeground))
				.textFieldStyle(.plain)
				.font(.system(size: 14))
				.foregroundStyle(Palette.foreground)
				.focused($focused)
				.onExitCommand { text = "" }
		}
		.padding(.horizontal, 12)
		.frame(height: 36)
		.background(Palette.background, in: Squircle(cornerRadius: 11))
		.overlay {
			Squircle(cornerRadius: 11)
				.strokeBorder(focused ? Palette.ring : Palette.border, lineWidth: 1)
		}
		.accessibilityElement(children: .contain)
		.accessibilityIdentifier("pane.extensions.search")
	}
}

// A select trigger (32 points, a 1px border): the profile whose extensions
// show, with its color.
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
					Capsule(style: .circular)
						.fill(selection.color.color)
						.frame(width: 4, height: 14)
				}
				Text(selection?.name ?? "Profile")
					.font(.system(size: 13))
					.foregroundStyle(Palette.foreground)
				Image(systemName: "chevron.down")
					.font(.system(size: 10, weight: .semibold))
					.foregroundStyle(Palette.mutedForeground)
			}
			.padding(.horizontal, 12)
			.frame(height: 32)
			.background(Palette.background, in: Squircle(cornerRadius: 11))
			.overlay {
				Squircle(cornerRadius: 11)
					.strokeBorder(Palette.border, lineWidth: 1)
			}
		}
		.menuStyle(.button)
		.buttonStyle(.plain)
		.menuIndicator(.hidden)
		.fixedSize()
		.accessibilityLabel("Profile")
		.accessibilityIdentifier("pane.extensions.profile")
	}
}

// A card, rounded 15 with a 1px border, p-4: the 40-point icon, the name and
// version, two lines of description, and the pin at the top; Options (when
// the extension has a page) and Remove at the bottom, the switch at its end.
// A disabled extension's icon and text dim.
private struct ExtensionCard: View {
	let item: ExtensionInfo
	let setEnabled: @MainActor (Bool) -> Void
	let setPinned: @MainActor (Bool) -> Void
	let options: () -> Void
	let remove: () -> Void

	static func identifier(_ extensionID: String) -> String {
		"pane.extensions.extension.\(extensionID)"
	}

	var body: some View {
		VStack(alignment: .leading, spacing: 16) {
			HStack(alignment: .top, spacing: 14) {
				Group {
					if let icon = item.icon {
						Image(nsImage: icon)
							.resizable()
							.interpolation(.high)
					} else {
						Palette.secondary
					}
				}
				.frame(width: 40, height: 40)
				.clipShape(Squircle(cornerRadius: 11))

				VStack(alignment: .leading, spacing: 3) {
					HStack(alignment: .firstTextBaseline, spacing: 6) {
						Text(item.name)
							.font(.system(size: 13, weight: .medium))
							.foregroundStyle(Palette.accentForeground)
							.lineLimit(1)
						Text(item.version)
							.font(.system(size: 11))
							.foregroundStyle(Palette.mutedForeground)
							.fixedSize()
					}

					if !item.summary.isEmpty {
						Text(item.summary)
							.font(.system(size: 12))
							.foregroundStyle(Palette.mutedForeground)
							.lineLimit(2)
							.lineSpacing(2)
					}
				}
				.frame(maxWidth: .infinity, alignment: .leading)
				.opacity(item.enabled ? 1 : 0.6)

				if item.hasAction {
					PinButton(pinned: item.pinned) { setPinned(!item.pinned) }
						.disabled(!item.enabled)
						.accessibilityIdentifier("pane.extensions.pin.\(item.id)")
				}
			}
			.frame(maxHeight: .infinity, alignment: .top)

			HStack(spacing: 8) {
				if item.optionsURL != nil {
					Button("Options", action: options)
						.buttonStyle(.pane(.outline, size: .small))
						.accessibilityIdentifier("pane.extensions.options.\(item.id)")
				}

				Button("Remove", action: remove)
					.buttonStyle(.pane(.outline, size: .small))
					.accessibilityIdentifier("pane.extensions.remove.\(item.id)")

				Spacer()

				Toggle(isOn: Binding(get: { item.enabled }, set: setEnabled)) {
					EmptyView()
				}
				.toggleStyle(.paneSwitch)
				.accessibilityLabel("Enabled")
			}
		}
		.padding(16)
		.frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
		.background(Palette.card, in: Squircle(cornerRadius: 15))
		.overlay {
			Squircle(cornerRadius: 15)
				.strokeBorder(Palette.border, lineWidth: 1)
		}
		.accessibilityElement(children: .contain)
		.accessibilityIdentifier(Self.identifier(item.id))
	}
}

// The extensions menu's pin, on a 28-point circle: what a click does, as
// there (the pin struck through, in blue, while pinned), the accent fill on
// hover.
private struct PinButton: View {
	let pinned: Bool
	let action: () -> Void

	@State private var isHovered = false
	@Environment(\.isEnabled) private var isEnabled

	var body: some View {
		Button(action: action) {
			Image(nsImage: (pinned ? Glyph.pinOff : Glyph.pin).image(strokeWidth: 1.75, size: 16))
				.renderingMode(.template)
				.foregroundStyle(pinned ? Palette.pinned : Palette.mutedForeground)
				.frame(width: 28, height: 28)
				.background(isHovered ? Palette.accent : .clear, in: Circle())
				.contentShape(Circle())
		}
		.buttonStyle(.plain)
		.opacity(isEnabled ? 1 : 0.5)
		.onHover { isHovered = $0 && isEnabled }
		.help(pinned ? "Unpin from toolbar" : "Pin to toolbar")
		.accessibilityLabel(pinned ? "Unpin from toolbar" : "Pin to toolbar")
	}
}
