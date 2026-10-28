import PaneKit
import SwiftUI

// The Extensions page (Paper: 08 · Extensions), Pane's chrome://extensions,
// as Chrome and Aside have theirs: a profile's extensions as cards, two to a
// row, found by name or description, each with its options, removal, pin,
// and switch. Extensions come from the Chrome Web Store's own Add to Chrome.
// Extensions are per profile, so a profile select leads, on the active one.
struct ExtensionsPage: View {
	let controller: BrowserController

	@State private var profileID: String?
	@State private var query = ""

	private var model: BrowserModel { controller.model }
	// The profile chosen in the select while it exists, else the active one,
	// else the first: deleting the chosen profile brings the page back to
	// one that is there.
	private var selectedProfile: BrowserProfile? {
		profileID.flatMap(model.profile) ?? model.activeProfileID.flatMap(model.profile) ?? model.profiles.first
	}
	private var selectedProfileID: String? { selectedProfile?.id }
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

			if let selectedProfile {
				// Its border in the profile's color at 40%, as the active
				// profile's row in the sidebar.
				PaneMenuButton(
					options: model.profiles.map { ($0.name, $0.id) },
					selection: selectedProfile.id,
					label: "Profile",
					ring: selectedProfile.color.color.opacity(0.4)
				) { profileID = $0 }
				.accessibilityIdentifier("pane.extensions.profile")

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
			Text("Add them from the Chrome Web Store.")
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
}

// A text field with a magnifying glass: what the cards are narrowed to.
private struct SearchField: View {
	@Binding var text: String

	@FocusState private var focused: Bool

	var body: some View {
		HStack(spacing: 8) {
			// 12.5 points of ink, as the SF Symbol it replaced at 13.
			LucideIcon(.search, size: 15)
				.foregroundStyle(Palette.mutedForeground)

			TextField("", text: $text, prompt: Text("Search extensions").foregroundStyle(Palette.mutedForeground))
				.textFieldStyle(.plain)
				.font(.system(size: 14))
				.foregroundStyle(Palette.foreground)
				.focused($focused)
				.onExitCommand { text = "" }
				.accessibilityLabel("Search extensions")
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

	var body: some View {
		let tint = pinned ? Palette.pinned : Palette.mutedForeground

		Button(action: action) {
			Image(nsImage: (pinned ? Glyph.pinOff : Glyph.pin).image(strokeWidth: 1.75, size: 16))
				.renderingMode(.template)
		}
		.buttonStyle(.icon(size: 28, tint: tint, hoverTint: tint))
		.help(pinned ? "Unpin from toolbar" : "Pin to toolbar")
		.accessibilityLabel(pinned ? "Unpin from toolbar" : "Pin to toolbar")
	}
}
