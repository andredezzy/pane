import PaneKit
import SwiftUI

// The Extensions page (Paper: 09 · Extensions), Pane's chrome://extensions,
// as Chrome and Aside have theirs: a profile's extensions as cards, two to a
// row, found by name or description, each with its details, options,
// removal, pin, and switch; or one extension's Details
// (ExtensionDetailsView). Extensions come from the Chrome Web Store's own install button, which the engine labels Add to Pane.
// Extensions are per profile, so a profile select leads, on the active one.
struct ExtensionsPage: View {
	let model: BrowserModel
	let controller: ExtensionsController

	@State private var profileID: String?
	@State private var query = ""
	// The profile chosen in the select while it exists, else the active one,
	// else the first: deleting the chosen profile brings the page back to
	// one that is there.
	private var selectedProfile: BrowserProfile? {
		profileID.flatMap(model.profile) ?? model.activeProfileID.flatMap(model.profile) ?? model.profiles.first
	}
	private var selectedProfileID: String? { selectedProfile?.id }
	private var extensions: [ExtensionInfo] { selectedProfileID.map(controller.list) ?? [] }

	private var shown: [ExtensionInfo] {
		let query = query.trimmingCharacters(in: .whitespaces)

		guard !query.isEmpty else {
			return extensions
		}

		return extensions.filter { $0.name.localizedCaseInsensitiveContains(query) || $0.summary.localizedCaseInsensitiveContains(query) }
	}

	var body: some View {
		Group {
			if let shown = controller.details, let item = controller.list(shown.profileID).first(where: { $0.id == shown.extensionID }) {
				ExtensionDetailsView(
					controller: controller,
					item: item,
					shown: shown,
					profileName: model.profile(shown.profileID)?.name ?? "this profile"
				)
			} else {
				list
			}
		}
		.onAppear {
			selectedProfileID.map(controller.refresh)
		}
		// Back from Details, the list is its profile's.
		.onChange(of: controller.details?.profileID, initial: true) { _, id in
			if let id {
				profileID = id
			}
		}
		.onChange(of: profileID) { _, id in
			if let id {
				controller.openContextIfNeeded(profileID: id)
			}
		}
	}

	private var list: some View {
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
									.lineBox(15)
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
			// Back from Details, the list shows the extension's card, once the
			// cards are laid out.
			.onChange(of: controller.pageAnchor, initial: true) { _, anchor in
				guard let anchor else {
					return
				}

				DispatchQueue.main.async {
					proxy.scrollTo(anchor, anchor: .top)
					controller.pageAnchor = nil
				}
			}
		}
		.background(Palette.card)
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
				// Its border in the profile's color at 40% (ADR 0062).
				PaneMenuButton(
					options: model.profiles.map { ($0.name, $0.id) },
					selection: selectedProfile.id,
					label: "Profile",
					ring: selectedProfile.color.color.opacity(0.4)
				) { profileID = $0 }
				.accessibilityIdentifier("pane.extensions.profile")
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
				selectedProfileID.map { controller.setEnabled(item.id, enabled, inProfile: $0) }
			},
			setPinned: { pinned in
				selectedProfileID.map { controller.setPinned(item.id, pinned, inProfile: $0) }
			},
			details: { controller.showDetails(item.id, inProfile: selectedProfileID) },
			options: { controller.openOptions(item, inProfile: selectedProfileID) },
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
				.lineBox(18)
			Text("Add them from the Chrome Web Store.")
				.font(.system(size: 12))
				.foregroundStyle(Palette.mutedForeground)
				.lineBox(18)
			Button("Visit Chrome Web Store") { controller.visitWebStore(inProfile: selectedProfileID) }
				.buttonStyle(.pane(.outline, size: .small))
				.padding(.top, 10)
		}
		.frame(maxWidth: .infinity)
		.padding(.vertical, 48)
		.overlay {
			Squircle(cornerRadius: 15)
				.strokeBorder(Palette.border, style: StrokeStyle(lineWidth: 1, dash: [3, 2]))
		}
	}
}

// A text field with a magnifying glass: what the cards are narrowed to. Its
// border alone, over the page, with no fill (the user's call).
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
				.font(.system(size: 13))
				.foregroundStyle(Palette.foreground)
				.focused($focused)
				.onExitCommand { text = "" }
				.accessibilityLabel("Search extensions")
		}
		// 12 inside the 1px border, as Paper's field.
		.padding(.horizontal, 13)
		.frame(height: 36)
		// A click anywhere in the border focuses the field, as a search
		// field's does; with no fill, its padding would let the click through.
		.contentShape(Squircle(cornerRadius: 11))
		.onTapGesture { focused = true }
		.overlay {
			Squircle(cornerRadius: 11)
				.strokeBorder(Palette.border, lineWidth: 1)
		}
		// Focused, the ring 1 point outside the border, as a field's.
		.overlay {
			if focused {
				Squircle(cornerRadius: 12)
					.strokeBorder(Palette.ring, lineWidth: 1)
					.padding(-1)
			}
		}
		.accessibilityElement(children: .contain)
		.accessibilityIdentifier("pane.extensions.search")
	}
}

// A card, rounded 15 with a 1px border, p-4: the 40-point icon, the name,
// two lines of description, and the pin at the top; Details, Options (when
// the extension has a page, which opens while it is enabled), and Remove at
// the bottom, the switch at its end. The version is in Details (Paper:
// 09.15). A disabled extension's icon and text dim.
private struct ExtensionCard: View {
	let item: ExtensionInfo
	let setEnabled: @MainActor (Bool) -> Void
	let setPinned: @MainActor (Bool) -> Void
	let details: () -> Void
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
				// A disabled extension dims its icon with its text.
				.opacity(item.enabled ? 1 : 0.6)

				VStack(alignment: .leading, spacing: 3) {
					Text(item.name)
						.font(.system(size: 13, weight: .medium))
						.foregroundStyle(Palette.accentForeground)
						.lineLimit(1)
						.lineBox(18)

					if !item.summary.isEmpty {
						// 18-point lines.
						Text(item.summary)
							.font(.system(size: 12))
							.foregroundStyle(Palette.mutedForeground)
							.lineLimit(2)
							.lineSpacing(3)
							.padding(.vertical, 1.5)
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
				Button("Details", action: details)
					.buttonStyle(.pane(.outline, size: .small))
					.accessibilityIdentifier("pane.extensions.details.\(item.id)")

				if item.optionsURL != nil {
					Button("Options", action: options)
						.buttonStyle(.pane(.outline, size: .small))
						.disabled(!item.enabled)
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
		// 16 inside the 1-point border, as CSS's p-4 within it.
		.padding(17)
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
