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

	// 39px, after the traffic lights (82px in): the title and Collapse all.
	private var header: some View {
		HStack(spacing: 8) {
			Text("Pane")
				.font(.system(size: 12, weight: .semibold))
				.foregroundStyle(Palette.mutedForeground)

			Spacer()

			CollapseAllButton(isEnabled: !model.expandedProfileIDs.isEmpty) {
				for id in model.expandedProfileIDs {
					controller.unloadProfile(id)
				}
				model.expandedProfileIDs.removeAll()
			}
		}
		.padding(.leading, 82)
		.padding(.trailing, 10)
		.frame(height: 39)
	}

	// px-2.5 pb-2: New, a hairline, Settings.
	private var footer: some View {
		VStack(alignment: .leading, spacing: 6) {
			// The 14px plus makes this row 32px; the others are 28.
			SidebarButton(isSelected: false, tintsOnHover: false, height: 32, action: onNewProfile) {
				Text("+")
					.font(.system(size: 14, weight: .light))
				Text("New")
			}
			.accessibilityLabel("New")
			.accessibilityIdentifier("pane.sidebar.new")

			Rectangle()
				.fill(Palette.foreground.opacity(0.04))
				.frame(height: 1)

			SidebarButton(isSelected: model.page == .settings) {
				model.page = model.page == .settings ? .browser : .settings
			} label: {
				LucideIcon(.settings, size: 14)
				Text("Settings")
			}
			.accessibilityLabel("Settings")
			.accessibilityIdentifier("pane.sidebar.settings")
		}
		.padding(.horizontal, 10)
		.padding(.bottom, 8)
	}
}

// 24px, rounded 4, muted; hover fills with the accent color.
private struct CollapseAllButton: View {
	let isEnabled: Bool
	let action: () -> Void

	@State private var isHovered = false
	@Environment(\.accessibilityReduceMotion) private var reduceMotion

	var body: some View {
		Button(action: action) {
			LucideIcon(.chevronsDownUp, size: 14)
				.frame(width: 24, height: 24)
				.contentShape(Rectangle())
		}
		.buttonStyle(.plain)
		.foregroundStyle(isHovered ? Palette.accentForeground : Palette.mutedForeground)
		.background(isHovered ? Palette.accent : .clear, in: RoundedRectangle(cornerRadius: 4, style: .circular))
		.opacity(isEnabled ? 1 : 0.4)
		.disabled(!isEnabled)
		.onHover { hovering in
			withAnimation(reduceMotion ? nil : .easeOut(duration: 0.15)) { isHovered = hovering && isEnabled }
		}
		.help("Collapse all profiles")
		.accessibilityLabel("Collapse all profiles")
	}
}

// px-2 py-1.5, rounded-md, 12px. Selected (Settings, while shown) is
// foreground/5; hover is the accent color.
private struct SidebarButton<Label: View>: View {
	let isSelected: Bool
	// New keeps its muted text on hover; Settings darkens.
	var tintsOnHover = true
	var height: CGFloat = 28
	let action: () -> Void
	@ViewBuilder let label: Label

	@State private var isHovered = false

	var body: some View {
		Button(action: action) {
			HStack(spacing: 6) {
				label
				Spacer(minLength: 0)
			}
			.font(.system(size: 12))
			.padding(.horizontal, 8)
			.frame(height: height)
			.contentShape(Rectangle())
		}
		.buttonStyle(.plain)
		.foregroundStyle(isSelected || (tintsOnHover && isHovered) ? Palette.accentForeground : Palette.mutedForeground)
		.hoverBackground(isSelected: isSelected, cornerRadius: 6)
		.onHover { isHovered = $0 }
	}
}

// A profile row (30px: py-1.5 and a 1px border) and, expanded, its tabs
// (24px, 4px apart) and New tab. Profiles are 6px apart.
private struct ProfileSection: View {
	let controller: BrowserController
	let profile: BrowserProfile
	let onEdit: () -> Void

	@State private var confirmingDelete = false
	// The moon marks an unload that happened this session: a profile never
	// loaded shows none.
	@State private var wasLoaded = false
	@State private var isHovered = false

	private var model: BrowserModel { controller.model }
	private var isExpanded: Bool { model.expandedProfileIDs.contains(profile.id) }
	private var isActive: Bool { model.activeProfileID == profile.id }
	private var hasLoadedTabs: Bool { profile.tabs.contains { model.loadedTabIDs.contains($0.id) } }
	private var isUnloaded: Bool { wasLoaded && !hasLoadedTabs && !profile.tabs.isEmpty }

	var body: some View {
		VStack(alignment: .leading, spacing: 6) {
			header

			if isExpanded {
				VStack(alignment: .leading, spacing: 4) {
					ForEach(profile.tabs) { tab in
						TabRow(controller: controller, tab: tab, profileID: profile.id)
					}

					// mt-1 against the last tab's 4px margin: CSS collapses
					// them to one 4px gap.
					NewTabRow { controller.openTab(inProfile: profile.id) }
				}
			}
		}
		.onChange(of: hasLoadedTabs, initial: true) { _, loaded in
			if loaded {
				wasLoaded = true
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
		let color = profile.color.color

		return Button(action: toggle) {
			HStack(spacing: 6) {
				RoundedRectangle(cornerRadius: 2, style: .circular)
					.fill(color)
					.frame(width: 4, height: 14)

				Text(profile.name)
					.font(.system(size: 12, weight: .medium))
					.lineLimit(1)
					.truncationMode(.tail)
					.frame(maxWidth: .infinity, alignment: .leading)

				if isUnloaded {
					LucideIcon(.moon, size: 12)
						.foregroundStyle(Palette.mutedForeground.opacity(0.5))
						.accessibilityLabel("Unloaded")
				}

				if let status = model.proxyStatus[profile.id] {
					ProxyDot(status: status)
				}

				if !isExpanded && !profile.tabs.isEmpty {
					Text("\(profile.tabs.count)")
						.font(.system(size: 10))
						.foregroundStyle(Palette.mutedForeground)
				}
			}
			// px-2 inside a 1px border (transparent unless active).
			.padding(.horizontal, 9)
			.frame(height: 30)
			.contentShape(Rectangle())
		}
		.buttonStyle(.plain)
		.foregroundStyle(isActive ? Palette.accentForeground : Palette.mutedForeground)
		.background {
			RoundedRectangle(cornerRadius: 6, style: .circular)
				.fill(isActive ? color.opacity(0.04) : .clear)
				.strokeBorder(isActive ? color.opacity(0.4) : .clear, lineWidth: 1)
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

// The proxy's last test: yellow while testing, emerald when connected, red
// when it failed (6px).
private struct ProxyDot: View {
	let status: BrowserModel.ProxyStatus

	var body: some View {
		Circle()
			.fill(color)
			.frame(width: 6, height: 6)
			.accessibilityLabel("Proxy \(status.rawValue)")
	}

	private var color: Color {
		switch status {
		case .testing: Color(rgb: 0xEAB308)
		case .connected: Color(rgb: 0x10B981)
		case .failed: Color(rgb: 0xEF4444)
		}
	}
}

// px-2 py-1, rounded 5, 12px: the favicon (or a globe), the title, and a
// close X on hover. Selected is foreground/5; unloaded tabs are at 60%.
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
				FaviconView(dataURL: tab.favicon)

				Text(tab.title.isEmpty ? "Loading..." : tab.title)
					.font(.system(size: 12))
					.lineLimit(1)
					.truncationMode(.tail)
					.frame(maxWidth: .infinity, alignment: .leading)

				Button { controller.closeTab(tab.id) } label: {
					LucideIcon(.x, size: 12)
				}
				.buttonStyle(.plain)
				.opacity(isHovered ? 1 : 0)
				.accessibilityLabel("Close tab")
			}
			.padding(.horizontal, 8)
			.frame(height: 24)
			.contentShape(Rectangle())
		}
		.buttonStyle(.plain)
		.foregroundStyle(isActive || isHovered ? Palette.accentForeground : Palette.mutedForeground)
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

// "+ New tab": a 10px plus, px-2 py-1, muted until hovered.
private struct NewTabRow: View {
	let action: () -> Void

	@State private var isHovered = false

	var body: some View {
		Button(action: action) {
			HStack(spacing: 6) {
				Text("+")
					.font(.system(size: 10))
				Text("New tab")
					.font(.system(size: 12))
				Spacer(minLength: 0)
			}
			.padding(.horizontal, 8)
			.frame(height: 24)
			.contentShape(Rectangle())
		}
		.buttonStyle(.plain)
		.foregroundStyle(isHovered ? Palette.accentForeground : Palette.mutedForeground)
		.hoverBackground(isSelected: false)
		.onHover { isHovered = $0 }
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
