import PaneCore
import PaneKit
import SwiftUI

// An extension's Details on the Extensions page (Paper: 09.13, 09.14,
// 09.16), as chrome://extensions' details page, in Settings' layout: the
// back arrow to the list, the icon, the name, its version, size, and source,
// and the switch; the description; Permissions, in Chrome's words; Site
// access, for an extension that asks for every site, with its sites On
// specific sites; Pin to toolbar and Allow access to file URLs; its options,
// website, and Web Store page; Remove extension. 640 wide at most, as
// Settings.
struct ExtensionDetailsView: View {
	let controller: ExtensionsController
	let item: ExtensionInfo
	let shown: ShownDetails
	let profileName: String

	private var engine: ExtensionDetails? { shown.engine }
	private var profileID: String { shown.profileID }

	var body: some View {
		ScrollView {
			VStack(alignment: .leading, spacing: 20) {
				header

				VStack(alignment: .leading, spacing: 20) {
					if !item.summary.isEmpty {
						// 13 on 20-point lines, the foreground at 70%.
						Text(item.summary)
							.font(.system(size: 13))
							.foregroundStyle(Palette.foreground)
							.lineSpacing(4)
							.padding(.vertical, 2)
							.opacity(0.7)
							.padding(.horizontal, 15)
							.fixedSize(horizontal: false, vertical: true)
					}

					if let engine {
						permissions(engine.permissions)

						if engine.siteAccess != .none {
							SettingsGroup(title: "Site access") {
								siteAccess(engine)
							}
						}

						if item.hasAction || engine.wantsFileAccess {
							SettingsGroup {
								controls(engine)
							}
						}

						let links = links(engine)

						if !links.isEmpty {
							SettingsGroup {
								ForEach(links.indices, id: \.self) { index in
									LinkRow(link: links[index], last: index == links.count - 1)
								}
							}
						}
					}

					SettingsGroup {
						SettingsRow(title: "Remove extension", description: "Removes it from \(profileName) only.", last: true) {
							Button("Remove") { controller.confirmUninstall(item, inProfile: profileID) }
								.buttonStyle(.pane(.outline, size: .small))
								.accessibilityIdentifier("pane.extensionDetails.remove")
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
		.accessibilityElement(children: .contain)
		.accessibilityIdentifier("pane.extensionDetails")
	}

	// Back, the 40-point icon, the name over "Version · size · source", and
	// the switch, 10 in from the cards' edge (their controls' line).
	private var header: some View {
		HStack(spacing: 12) {
			Button(action: controller.closeDetails) {
				LucideIcon(.arrowLeft, size: 16, strokeWidth: 1.75)
			}
			.buttonStyle(.icon(size: 28))
			.help("Back to extensions")
			.accessibilityLabel("Back to extensions")
			.accessibilityIdentifier("pane.extensionDetails.back")

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
			.opacity(item.enabled ? 1 : 0.6)

			VStack(alignment: .leading, spacing: 0) {
				Text(item.name)
					.font(.system(size: 14, weight: .medium))
					.foregroundStyle(Palette.accentForeground)
					.lineLimit(1)
					.lineBox(20)

				Text(subtitle)
					.font(.system(size: 12))
					.foregroundStyle(Palette.mutedForeground)
					.lineLimit(1)
					.lineBox(16)
			}
			.frame(maxWidth: .infinity, alignment: .leading)

			Toggle(isOn: Binding(get: { item.enabled }, set: { controller.setEnabled(item.id, $0, inProfile: profileID) })) {
				EmptyView()
			}
			.toggleStyle(.paneSwitch)
			.accessibilityLabel("Enabled")
		}
		.padding(.trailing, 10)
		.frame(maxWidth: 640)
	}

	// The size joins once measured.
	private var subtitle: String {
		["Version \(item.version)", shown.size, engine?.source].compactMap { $0 }.joined(separator: " \u{00B7} ")
	}

	// Each permission on a 20-point line, 2 apart, 12 and 15 in from the
	// card's border; Chrome's sentence when there is none.
	private func permissions(_ messages: [String]) -> some View {
		SettingsGroup(title: "Permissions") {
			VStack(alignment: .leading, spacing: 2) {
				if messages.isEmpty {
					PermissionLine(text: "This extension requires no special permissions", color: Palette.mutedForeground)
				}

				ForEach(messages, id: \.self) { message in
					PermissionLine(text: message, color: Palette.foreground)
				}
			}
			.padding(.horizontal, 16)
			.padding(.vertical, 12)
			.frame(maxWidth: .infinity, alignment: .leading)
			.accessibilityIdentifier("pane.extensionDetails.permissions")
		}
	}

	// The pop-up, Chrome's choices; On specific sites, the sites it was
	// granted under it (Paper: 09.16), each with its ×, then Add a site.
	@ViewBuilder
	private func siteAccess(_ engine: ExtensionDetails) -> some View {
		let specific = engine.siteAccess == .onSpecificSites

		SettingsRow(title: "Allow this extension to read and change all your data on websites you visit", last: !specific) {
			PaneMenuButton(
				options: [("On click", ExtensionSiteAccess.onClick), ("On specific sites", .onSpecificSites), ("On all sites", .onAllSites)],
				selection: engine.siteAccess,
				label: "Site access",
				chevron: .upDown
			) { access in
				controller.setSiteAccess(item.id, access, inProfile: profileID)
			}
			.accessibilityIdentifier("pane.extensionDetails.siteAccess")
		}

		if specific {
			ForEach(engine.specificSites, id: \.self) { site in
				SiteRow(title: ExtensionSite.title(site)) {
					controller.removeSite(site, of: item.id, inProfile: profileID)
				}
			}

			LinkRow(
				link: DetailsLink(title: "Add a site", identifier: "addSite", icon: .plus) {
					controller.showAddSite(item.id, inProfile: profileID)
				},
				last: true
			)
		}
	}

	@ViewBuilder
	private func controls(_ engine: ExtensionDetails) -> some View {
		if item.hasAction {
			SettingsRow(title: "Pin to toolbar", minHeight: 48, last: !engine.wantsFileAccess) {
				Toggle(isOn: Binding(get: { item.pinned }, set: { controller.setPinned(item.id, $0, inProfile: profileID) })) {
					EmptyView()
				}
				.toggleStyle(.paneSwitch)
				.disabled(!item.enabled)
				.accessibilityLabel("Pin to toolbar")
			}
		}

		if engine.wantsFileAccess {
			SettingsRow(title: "Allow access to file URLs", minHeight: 48, last: true) {
				Toggle(isOn: Binding(get: { engine.allowsFileAccess }, set: { controller.setFileAccess(item.id, $0, inProfile: profileID) })) {
					EmptyView()
				}
				.toggleStyle(.paneSwitch)
				.accessibilityLabel("Allow access to file URLs")
			}
		}
	}

	// Each opens in a new tab of the profile. Options needs the extension
	// enabled, as Chrome's.
	private func links(_ engine: ExtensionDetails) -> [DetailsLink] {
		var links: [DetailsLink] = []

		if item.optionsURL != nil {
			links.append(DetailsLink(title: "Extension options", identifier: "options", enabled: item.enabled) {
				controller.openOptions(item, inProfile: profileID)
			})
		}

		if let url = engine.homepageURL {
			links.append(DetailsLink(title: "Open extension website", identifier: "website") {
				controller.openLink(url, inProfile: profileID)
			})
		}

		if let url = engine.webStoreURL {
			links.append(DetailsLink(title: "View in Chrome Web Store", identifier: "webStore") {
				controller.openLink(url, inProfile: profileID)
			})
		}

		return links
	}
}

private struct PermissionLine: View {
	let text: String
	let color: Color

	var body: some View {
		// 13 on 20-point lines (16 of its own).
		Text(text)
			.font(.system(size: 13))
			.foregroundStyle(color)
			.lineSpacing(4)
			.padding(.vertical, 2)
			.fixedSize(horizontal: false, vertical: true)
	}
}

private struct DetailsLink {
	let title: String
	let identifier: String
	var enabled = true
	var icon = Lucide.externalLink
	let open: () -> Void
}

// A site of the list: its title, and the × that takes it away, ending 10 in
// from the card's edge, as the pop-up above it.
private struct SiteRow: View {
	let title: String
	let remove: () -> Void

	var body: some View {
		HStack(spacing: 16) {
			Text(title)
				.font(.system(size: 13))
				.foregroundStyle(Palette.foreground)
				.lineLimit(1)
				.truncationMode(.middle)
				.lineBox(18)
				.frame(maxWidth: .infinity, alignment: .leading)

			Button(action: remove) {
				LucideIcon(.x, size: 16, strokeWidth: 1.75)
			}
			.buttonStyle(.icon(size: 28))
			.help("Remove \(title)")
			.accessibilityLabel("Remove \(title)")
		}
		.padding(.leading, 16)
		.padding(.trailing, 11)
		.frame(minHeight: 48)
		.overlay(alignment: .bottom) {
			Hairline()
		}
		.accessibilityElement(children: .contain)
		.accessibilityIdentifier("pane.extensionDetails.site.\(title)")
	}
}

// A row that opens a page, or Add a site's dialog: its title, and its icon
// (the external link, the plus) 16 in from the card's edge; the whole row
// takes the outline hover's fill.
private struct LinkRow: View {
	let link: DetailsLink
	let last: Bool

	@State private var isHovered = false

	var body: some View {
		Button(action: link.open) {
			HStack(spacing: 16) {
				Text(link.title)
					.font(.system(size: 13))
					.foregroundStyle(Palette.foreground)
					.lineBox(18)
					.frame(maxWidth: .infinity, alignment: .leading)

				LucideIcon(link.icon, size: 14, strokeWidth: 1.75)
					.foregroundStyle(Palette.mutedForeground)
			}
			.padding(.leading, 16)
			.padding(.trailing, 17)
			.frame(minHeight: 48)
			.background(Palette.outlineHover.opacity(isHovered && link.enabled ? 1 : 0))
			.contentShape(Rectangle())
			.overlay(alignment: .bottom) {
				if !last {
					Hairline()
				}
			}
		}
		.buttonStyle(.plain)
		.disabled(!link.enabled)
		.opacity(link.enabled ? 1 : 0.5)
		.onHover { hovering in
			withAnimation(Motion.hover) { isHovered = hovering }
		}
		.accessibilityIdentifier("pane.extensionDetails.\(link.identifier)")
	}
}

// Details' Add a site (Paper: 09.16; Chrome's runtime hosts dialog), as the
// app's other dialogs: its title, the Site field, Chrome's message for a
// site it refuses, then Cancel (Esc) and Add (Return), which waits for
// something typed. The controller takes the two keys (KeyRouting).
struct AddSiteDialogView: View {
	let controller: ExtensionsController
	let dialog: AddSiteDialog

	var body: some View {
		VStack(alignment: .leading, spacing: 16) {
			Text("Add a site")
				.font(.system(size: 18, weight: .semibold))
				.tracking(-0.18)
				.foregroundStyle(Palette.foreground)
				.lineBox(24)

			VStack(alignment: .leading, spacing: 6) {
				LabeledField(title: "Site") {
					PaneTextField(
						placeholder: "e.g. https://example.com",
						text: Binding(get: { dialog.site }, set: controller.editSite),
						focusesOnAppear: true
					)
					.accessibilityIdentifier("pane.addSite.field")
				}

				if dialog.invalid {
					Text("Not a valid web address")
						.font(.system(size: 12, weight: .medium))
						.foregroundStyle(Palette.destructive)
						.lineBox(16)
				}
			}

			HStack(spacing: 8) {
				Spacer()

				Button(action: controller.cancelAddSite) {
					KeyedLabel(title: "Cancel", key: "Esc", role: .cancel)
				}
				.buttonStyle(.keyed(.cancel))

				Button(action: controller.addSite) {
					KeyedLabel(title: "Add", key: "↩", role: .primary)
				}
				.buttonStyle(.keyed(.primary))
				.disabled(dialog.site.isEmpty)
				.accessibilityIdentifier("pane.addSite.add")
			}
			.padding(.top, 8)
		}
		.padding(24)
		.frame(width: 400, alignment: .leading)
		.surfaceBackground(cornerRadius: 29, shadow: Palette.dialogShadow, blur: 64, y: 24)
		.accessibilityElement(children: .contain)
		.accessibilityAddTraits(.isModal)
		.accessibilityIdentifier("pane.addSite")
	}
}
