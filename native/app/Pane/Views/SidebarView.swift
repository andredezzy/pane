import PaneKit
import SwiftUI
import UniformTypeIdentifiers

// Profiles with their tabs. A profile expands to load its tabs and collapses
// to release them (CONTEXT.md: Unload); the selected tab is highlighted.
struct SidebarView: View {
	let controller: BrowserController
	var onNewProfile: () -> Void
	var onEditProfile: (String) -> Void

	private var model: BrowserModel { controller.model }

	var body: some View {
		VStack(spacing: 0) {
			header

			ScrollView {
				VStack(alignment: .leading, spacing: 6) {
					ForEach(model.profiles) { profile in
						ProfileSection(
							controller: controller,
							profile: profile,
							onEdit: { onEditProfile(profile.id) }
						)
					}
				}
				.padding(.horizontal, 10)
				.padding(.vertical, 4)
			}
			.accessibilityIdentifier("pane.sidebar.profiles")

			footer
		}
		.frame(maxHeight: .infinity, alignment: .top)
		.accessibilityElement(children: .contain)
		.accessibilityLabel("Sidebar")
		.accessibilityIdentifier("pane.sidebar")
	}

	private var header: some View {
		HStack(spacing: 8) {
			Text("Pane")
				.font(.system(size: 12, weight: .semibold))
				.foregroundStyle(.secondary)

			Spacer()

			Button {
				for id in model.expandedProfileIDs {
					controller.unloadProfile(id)
				}
				model.expandedProfileIDs.removeAll()
			} label: {
				Image(systemName: "rectangle.compress.vertical")
					.font(.system(size: 11))
					.frame(width: 24, height: 24)
			}
			.buttonStyle(.plain)
			.foregroundStyle(.secondary)
			.disabled(model.expandedProfileIDs.isEmpty)
			.help("Collapse all profiles")
			.accessibilityLabel("Collapse all profiles")
		}
		.padding(.leading, 82)
		.padding(.trailing, 10)
		.frame(height: 39)
	}

	private var footer: some View {
		VStack(alignment: .leading, spacing: 6) {
			SidebarButton(title: "New", systemImage: "plus", isSelected: false, action: onNewProfile)
				.accessibilityIdentifier("pane.sidebar.new")

			Rectangle()
				.fill(Color.primary.opacity(0.04))
				.frame(height: 1)

			SidebarButton(title: "Settings", systemImage: "gearshape", isSelected: model.page == .settings) {
				model.page = model.page == .settings ? .browser : .settings
			}
			.accessibilityIdentifier("pane.sidebar.settings")
		}
		.padding(.horizontal, 10)
		.padding(.bottom, 8)
	}
}

private struct SidebarButton: View {
	let title: String
	let systemImage: String
	let isSelected: Bool
	let action: () -> Void

	var body: some View {
		Button(action: action) {
			HStack(spacing: 6) {
				Image(systemName: systemImage)
					.font(.system(size: 11))
				Text(title)
					.font(.system(size: 12))
				Spacer()
			}
			.padding(.horizontal, 8)
			.padding(.vertical, 6)
			.contentShape(Rectangle())
		}
		.buttonStyle(.plain)
		.foregroundStyle(isSelected ? .primary : .secondary)
		.hoverBackground(isSelected: isSelected, cornerRadius: 6)
	}
}

private struct ProfileSection: View {
	let controller: BrowserController
	let profile: BrowserProfile
	let onEdit: () -> Void

	@State private var confirmingDelete = false

	private var model: BrowserModel { controller.model }
	private var isExpanded: Bool { model.expandedProfileIDs.contains(profile.id) }
	private var isActive: Bool { model.activeProfileID == profile.id }

	var body: some View {
		VStack(alignment: .leading, spacing: 4) {
			header

			if isExpanded {
				VStack(alignment: .leading, spacing: 4) {
					ForEach(profile.tabs) { tab in
						TabRow(controller: controller, tab: tab, profileID: profile.id)
					}

					NewTabRow { controller.openTab(inProfile: profile.id) }
				}
			}
		}
		.alert("Delete \u{201C}\(profile.name)\u{201D}?", isPresented: $confirmingDelete) {
			Button("Delete", role: .destructive) { controller.deleteProfile(profile.id) }
			Button("Cancel", role: .cancel) {}
		} message: {
			Text("This action cannot be undone.")
		}
	}

	private var header: some View {
		Button(action: toggle) {
			HStack(spacing: 6) {
				RoundedRectangle(cornerRadius: 2)
					.fill(profile.color.color)
					.frame(width: 4, height: 14)

				Text(profile.name)
					.font(.system(size: 12, weight: .medium))
					.lineLimit(1)
					.truncationMode(.tail)
					.frame(maxWidth: .infinity, alignment: .leading)

				if !isExpanded && !profile.tabs.isEmpty {
					Text("\(profile.tabs.count)")
						.font(.system(size: 10))
						.foregroundStyle(.secondary)
				}
			}
			.padding(.horizontal, 8)
			.padding(.vertical, 6)
			.contentShape(Rectangle())
		}
		.buttonStyle(.plain)
		.foregroundStyle(isActive ? .primary : .secondary)
		.background {
			RoundedRectangle(cornerRadius: 6, style: .continuous)
				.fill(isActive ? profile.color.color.opacity(0.04) : .clear)
				.strokeBorder(isActive ? profile.color.color.opacity(0.4) : .clear, lineWidth: 1)
		}
		.hoverBackground(isSelected: isActive, selectedFill: .clear, cornerRadius: 6)
		.contextMenu {
			Button("Edit profile", action: onEdit)
			Toggle("Keep loaded", isOn: Binding(
				get: { profile.keepLoaded },
				set: { value in model.updateProfile(profile.id) { $0.keepLoaded = value } }
			))
			Divider()
			Button("Delete profile", role: .destructive) { confirmingDelete = true }
		}
		.draggable(DraggedItem.profile(profile.id))
		.dropDestination(for: DraggedItem.self) { items, _ in
			guard case let .profile(id) = items.first,
			      let from = model.profiles.firstIndex(where: { $0.id == id }),
			      let to = model.profiles.firstIndex(where: { $0.id == profile.id }),
			      from != to
			else {
				return false
			}

			model.moveProfile(from: from, to: to)

			return true
		}
		.accessibilityIdentifier("pane.profile.\(profile.id)")
		.accessibilityLabel(profile.name)
	}

	private func toggle() {
		if isExpanded {
			model.expandedProfileIDs.remove(profile.id)
			controller.unloadProfile(profile.id)
		} else {
			model.expandedProfileIDs.insert(profile.id)
		}
	}
}

private struct TabRow: View {
	let controller: BrowserController
	let tab: PaneKit.Tab
	let profileID: String

	@State private var isHovered = false

	private var model: BrowserModel { controller.model }
	private var isActive: Bool { model.activeTabID == tab.id && model.page == .browser }
	private var isLoaded: Bool { model.loadedTabIDs.contains(tab.id) }

	var body: some View {
		Button { controller.select(tabID: tab.id) } label: {
			HStack(spacing: 6) {
				if model.loadingProgress[tab.id] != nil {
					ProgressView()
						.controlSize(.mini)
						.frame(width: 12, height: 12)
				} else {
					FaviconView(dataURL: tab.favicon)
				}

				Text(tab.title.isEmpty ? "Loading..." : tab.title)
					.font(.system(size: 12))
					.lineLimit(1)
					.truncationMode(.tail)
					.frame(maxWidth: .infinity, alignment: .leading)

				Button { controller.closeTab(tab.id) } label: {
					Image(systemName: "xmark")
						.font(.system(size: 9, weight: .semibold))
						.frame(width: 12, height: 12)
				}
				.buttonStyle(.plain)
				.opacity(isHovered ? 1 : 0)
				.accessibilityLabel("Close tab")
			}
			.padding(.horizontal, 8)
			.padding(.vertical, 4)
			.contentShape(Rectangle())
		}
		.buttonStyle(.plain)
		.foregroundStyle(isActive ? .primary : .secondary)
		.opacity(isLoaded || isActive ? 1 : 0.6)
		.hoverBackground(isSelected: isActive)
		.onHover { isHovered = $0 }
		.contextMenu {
			Button("Close tab") { controller.closeTab(tab.id) }
		}
		.draggable(DraggedItem.tab(tab.id))
		.dropDestination(for: DraggedItem.self) { items, _ in
			guard case let .tab(id) = items.first,
			      model.profileID(ofTab: id) == profileID,
			      let tabs = model.profile(profileID)?.tabs,
			      let from = tabs.firstIndex(where: { $0.id == id }),
			      let to = tabs.firstIndex(where: { $0.id == tab.id }),
			      from != to
			else {
				return false
			}

			model.moveTab(inProfile: profileID, from: from, to: to)

			return true
		}
		.accessibilityIdentifier("pane.tab.\(tab.id)")
		.accessibilityLabel(tab.title.isEmpty ? "Loading" : tab.title)
		.accessibilityAddTraits(isActive ? .isSelected : [])
	}
}

private struct NewTabRow: View {
	let action: () -> Void

	var body: some View {
		Button(action: action) {
			HStack(spacing: 6) {
				Text("+")
					.font(.system(size: 10))
					.frame(width: 12)
				Text("New tab")
					.font(.system(size: 12))
				Spacer()
			}
			.padding(.horizontal, 8)
			.padding(.vertical, 4)
			.contentShape(Rectangle())
		}
		.buttonStyle(.plain)
		.foregroundStyle(.secondary)
		.hoverBackground(isSelected: false)
	}
}

// What a sidebar drag carries: a profile or a tab, by id.
enum DraggedItem: Codable, Transferable {
	case profile(String)
	case tab(String)

	// JSON rather than a custom type: a custom one must be declared in the
	// bundle's Info.plist, which is the engine's (package.sh).
	static var transferRepresentation: some TransferRepresentation {
		CodableRepresentation(contentType: .json)
	}
}
