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

			// Fading at its edges as it scrolls, as the Electron app's list.
			FadingScrollView {
				VStack(alignment: .leading, spacing: 4) {
					ForEach(model.profiles) { profile in
						ProfileSection(
							controller: controller,
							profile: profile,
							onEdit: { onEditProfile(profile.id) }
						)
					}
				}
				.padding(.horizontal, 8)
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
				.font(.system(size: 13, weight: .semibold))
				.foregroundStyle(Palette.mutedForeground)
				// The title moves the window, as the rest of the header does.
				.allowsHitTesting(false)

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
		// On the traffic lights' center line: 19 points down (their 38-point
		// title bar), half a point above the 39-point row's own center.
		.padding(.bottom, 1)
		.frame(height: 39)
	}

	// px-2 pb-2: New, a hairline, Settings.
	private var footer: some View {
		VStack(alignment: .leading, spacing: 6) {
			if controller.updater.showsPill {
				UpdatePill(updater: controller.updater)
					.transition(.opacity.combined(with: .offset(y: 4)))
			}

			SidebarButton(isSelected: false, tintsOnHover: false, action: onNewProfile) {
				PlusMark()
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
				LucideIcon(.settings, size: 16)
				Text("Settings")
			}
			.accessibilityLabel("Settings")
			.accessibilityIdentifier("pane.sidebar.settings")
		}
		.padding(.horizontal, 8)
		.padding(.bottom, 8)
		.animation(.easeOut(duration: 0.3), value: controller.updater.showsPill)
	}
}

// A 28-point circle, muted; hover fills with the accent color.
private struct CollapseAllButton: View {
	let isEnabled: Bool
	let action: () -> Void

	@State private var isHovered = false
	@Environment(\.accessibilityReduceMotion) private var reduceMotion

	var body: some View {
		Button(action: action) {
			LucideIcon(.chevronsDownUp, size: 16)
				.frame(width: 28, height: 28)
				.contentShape(Circle())
		}
		.buttonStyle(.plain)
		.foregroundStyle(isHovered ? Palette.accentForeground : Palette.mutedForeground)
		.background(isHovered ? Palette.accent : .clear, in: Circle())
		.opacity(isEnabled ? 1 : 0.4)
		.disabled(!isEnabled)
		.onHover { hovering in
			withAnimation(reduceMotion ? nil : .easeOut(duration: 0.15)) { isHovered = hovering && isEnabled }
		}
		.help("Collapse all profiles")
		.accessibilityLabel("Collapse all profiles")
	}
}

// A 32-point row at 13, macOS's sidebar size, spaced as Aside's rows: the
// icon 7 in, the title 10 after it. Selected (Settings, while shown) is
// foreground/5; hover is the accent color.
private struct SidebarButton<Label: View>: View {
	let isSelected: Bool
	// New keeps its muted text on hover; Settings darkens.
	var tintsOnHover = true
	let action: () -> Void
	@ViewBuilder let label: Label

	@State private var isHovered = false

	var body: some View {
		Button(action: action) {
			HStack(spacing: 10) {
				label
				Spacer(minLength: 0)
			}
			.font(.system(size: 13))
			.padding(.leading, 7)
			.padding(.trailing, 8)
			.frame(height: SidebarRow.height)
			.contentShape(Rectangle())
		}
		.buttonStyle(.plain)
		.foregroundStyle(isSelected || (tintsOnHover && isHovered) ? Palette.accentForeground : Palette.mutedForeground)
		.hoverBackground(isSelected: isSelected, cornerRadius: SidebarRow.cornerRadius)
		.onHover { isHovered = $0 }
	}
}

// A profile row (36 points, inside a 1-point border) and, expanded, its tabs
// and New tab, 32 points each and 4 apart: Aside's 36-point rows. Profiles
// are 4 apart.
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
		VStack(alignment: .leading, spacing: 4) {
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
			HStack(spacing: 8) {
				Capsule(style: .circular)
					.fill(color)
					.frame(width: 4, height: 16)

				Text(profile.name)
					.font(.system(size: 13, weight: .medium))
					.lineLimit(1)
					.truncationMode(.tail)
					.frame(maxWidth: .infinity, alignment: .leading)

				if isUnloaded {
					LucideIcon(.moon, size: 14)
						.foregroundStyle(Palette.mutedForeground.opacity(0.5))
						.accessibilityLabel("Unloaded")
				}

				if let status = model.proxyStatus[profile.id] {
					ProxyDot(status: status)
				}

				if !isExpanded && !profile.tabs.isEmpty {
					Text("\(profile.tabs.count)")
						.font(.system(size: 12))
						.foregroundStyle(Palette.mutedForeground)
				}
			}
			// 7 in, inside a 1-point border (transparent unless active).
			.padding(.horizontal, 8)
			.frame(height: 36)
			.contentShape(Rectangle())
		}
		.buttonStyle(.plain)
		.foregroundStyle(isActive ? Palette.accentForeground : Palette.mutedForeground)
		.background {
			Squircle(cornerRadius: SidebarRow.cornerRadius)
				.fill(isActive ? color.opacity(0.04) : .clear)
				.strokeBorder(isActive ? color.opacity(0.4) : .clear, lineWidth: 1)
		}
		.hoverBackground(isSelected: isActive, selectedFill: .clear, cornerRadius: SidebarRow.cornerRadius)
		// The Electron app's menu, with its lucide icons.
		.contextMenu {
			Button(action: onEdit) {
				MenuLabel("Edit profile", icon: .pencil)
			}
			Toggle(isOn: Binding(
				get: { profile.keepLoaded },
				set: { value in model.updateProfile(profile.id) { $0.keepLoaded = value } }
			)) {
				MenuLabel("Keep loaded", icon: .timerOff)
			}
			Divider()
			Button(role: .destructive) { confirmingDelete = true } label: {
				MenuLabel("Delete profile", icon: .trash2)
			}
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

// A 32-point row at 13, macOS's sidebar size, spaced as Aside's tabs: the
// 16-point favicon (or a globe) 7 in, the title 10 after it, and a close X on
// hover. Selected is foreground/5; unloaded tabs are at 60%.
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
			HStack(spacing: 10) {
				FaviconView(dataURL: tab.favicon)

				Text(tab.title.isEmpty ? "Loading..." : tab.title)
					.font(.system(size: 13))
					.lineLimit(1)
					.truncationMode(.tail)
					.frame(maxWidth: .infinity, alignment: .leading)

				Button { controller.closeTab(tab.id) } label: {
					LucideIcon(.x, size: 14)
				}
				.buttonStyle(.plain)
				.opacity(isHovered ? 1 : 0)
				.accessibilityLabel("Close tab")
			}
			.padding(.leading, 7)
			.padding(.trailing, 8)
			.frame(height: SidebarRow.height)
			.contentShape(Rectangle())
		}
		.buttonStyle(.plain)
		.foregroundStyle(isActive || isHovered ? Palette.accentForeground : Palette.mutedForeground)
		.opacity(isLoaded || isActive ? 1 : 0.6)
		.hoverBackground(isSelected: isActive, cornerRadius: SidebarRow.cornerRadius)
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

// "+ New tab", a tab row's size, muted until hovered.
private struct NewTabRow: View {
	let action: () -> Void

	@State private var isHovered = false

	var body: some View {
		Button(action: action) {
			HStack(spacing: 10) {
				PlusMark()
				Text("New tab")
					.font(.system(size: 13))
				Spacer(minLength: 0)
			}
			.padding(.leading, 7)
			.padding(.trailing, 8)
			.frame(height: SidebarRow.height)
			.contentShape(Rectangle())
		}
		.buttonStyle(.plain)
		.foregroundStyle(isHovered ? Palette.accentForeground : Palette.mutedForeground)
		.hoverBackground(isSelected: false, cornerRadius: SidebarRow.cornerRadius)
		.onHover { isHovered = $0 }
	}
}

// The sidebar's rows, as Aside's: 32 points, rounded 15.
private enum SidebarRow {
	static let height: CGFloat = 32
	static let cornerRadius: CGFloat = 15
}

// New's and New tab's plus, in the favicon's 16-point slot.
private struct PlusMark: View {
	var body: some View {
		Text("+")
			.font(.system(size: 18, weight: .light))
			.frame(width: 16, height: 16)
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

// A menu item's title with its lucide icon, which takes the menu's text color.
private struct MenuLabel: View {
	let title: String
	let icon: Lucide

	init(_ title: String, icon: Lucide) {
		self.title = title
		self.icon = icon
	}

	var body: some View {
		Label {
			Text(title)
		} icon: {
			Image(nsImage: icon.menuImage)
		}
		// Menus show only a label's title unless asked for both.
		.labelStyle(.titleAndIcon)
	}
}
