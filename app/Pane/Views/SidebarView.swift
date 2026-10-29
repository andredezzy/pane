import PaneKit
import SwiftUI

// Profiles with their tabs. A profile expands to load its tabs and collapses
// to release them (CONTEXT.md: Unload); the selected tab is highlighted.
struct SidebarView: View {
	let controller: BrowserController
	var onNewProfile: () -> Void
	var onEditProfile: (String) -> Void

	private var model: BrowserModel { controller.model }

	// Profiles reorder by dragging their headers, as the Electron app's did.
	@State private var reorder = VerticalReorder(space: "pane.sidebar.profiles", rowHeight: 36, spacing: 4)

	var body: some View {
		VStack(spacing: 0) {
			header

			// Fading at its edges as it scrolls, as the Electron app's list, and
			// scrolled to the selected tab when it is out of view.
			ScrollViewReader { proxy in
				FadingScrollView {
					VStack(alignment: .leading, spacing: 4) {
						ForEach(reorder.order(model.profiles.map(\.id)), id: \.self) { id in
							if let profile = model.profile(id) {
								let isActive = isShown(id)

								// What it shows, as values: a section runs its body
								// again only when its own change, not the model's.
								ProfileSection(
									controller: controller,
									profile: profile,
									isActive: isActive,
									isExpanded: model.expandedProfileIDs.contains(id),
									proxyStatus: model.proxyStatus[id],
									selectedTabID: isActive ? model.activeTabID : nil,
									loadedTabIDs: Set(profile.tabs.lazy.map(\.id).filter(model.loadedTabIDs.contains)),
									reorder: reorder,
									onEdit: { onEditProfile(id) }
								)
								.equatable()
								.reorderRow(id, in: reorder)
							}
						}
					}
					.reorderList(reorder) { id in
						if let profile = model.profile(id) {
							ProfileHeaderContent(profile: profile, isActive: isShown(id), proxyStatus: model.proxyStatus[id], showsCount: !model.expandedProfileIDs.contains(id))
								.padding(.horizontal, 8)
								.frame(height: 36)
								.foregroundStyle(Palette.accentForeground)
								.dragCopy()
						}
					}
					.padding(.horizontal, 8)
					.padding(.vertical, 4)
				}
				.accessibilityIdentifier("pane.sidebar.profiles")
				.onChange(of: model.activeTabID, initial: true) { old, new in
					reveal(new, in: proxy, animated: old != new)
				}
			}

			footer
		}
		.frame(maxHeight: .infinity, alignment: .top)
		// The saved favicons decode as the window opens, before a profile
		// expands to show them.
		.task { Favicons.warm(model.profiles.flatMap(\.tabs)) }
		.accessibilityElement(children: .contain)
		.accessibilityLabel("Sidebar")
		.accessibilityIdentifier("pane.sidebar")
	}

	// The selected tab's row into view, wherever it was chosen (the switcher,
	// a shortcut, a new tab, a link): the least scroll that shows it whole and
	// clear of the edges' fades, none when it shows (the user's request). A
	// turn later, so a profile that opened for it has laid its rows out. At
	// launch it is there at once.
	private func reveal(_ tabID: String?, in proxy: ScrollViewProxy, animated: Bool) {
		guard let tabID else {
			return
		}

		DispatchQueue.main.async {
			withAnimation(animated ? Motion.reveal : nil) {
				proxy.scrollTo(RevealTarget(tabID: tabID))
			}
		}
	}

	// The profile whose page shows: none on Settings or Extensions, where no
	// tab is selected either. Its bar morphs back as the page opens.
	private func isShown(_ profileID: String) -> Bool {
		model.page == .browser && model.activeProfileID == profileID
	}

	// 39px, after the traffic lights (82px in): the title and Collapse all.
	private var header: some View {
		HStack(spacing: 8) {
			Text("Pane")
				.font(.system(size: 12, weight: .semibold))
				.foregroundStyle(Palette.mutedForeground)
				// The title moves the window, as the rest of the header does.
				.allowsHitTesting(false)

			Spacer()

			CollapseAllButton(isEnabled: !model.expandedProfileIDs.isEmpty, action: controller.collapseAll)
		}
		.padding(.leading, 82)
		// 8, as the rows below: Collapse all's edge meets theirs.
		.padding(.trailing, 8)
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

			Hairline(color: Palette.foreground.opacity(0.04))

			SidebarButton(isSelected: model.page == .settings, action: controller.toggleSettings) {
				LucideIcon(.settings, size: 16)
				Text("Settings")
			}
			.accessibilityLabel("Settings")
			.accessibilityIdentifier("pane.sidebar.settings")
		}
		.padding(.horizontal, 8)
		.padding(.bottom, 8)
		.animation(Motion.fade, value: controller.updater.showsPill)
	}
}

// A 28-point circle, muted; hover fills with the accent color.
private struct CollapseAllButton: View {
	let isEnabled: Bool
	let action: () -> Void

	var body: some View {
		Button(action: action) {
			LucideIcon(.chevronsDownUp, size: 16)
		}
		.buttonStyle(.icon(size: 28))
		.disabled(!isEnabled)
		.help("Collapse all profiles")
		.accessibilityLabel("Collapse all profiles")
	}
}

// A 32-point row at 12, spaced as Aside's rows: the icon 7 in, the title 10
// after it. Selected (Settings, while shown) is foreground/5; hover is the
// accent color.
private struct SidebarButton<Label: View>: View {
	let isSelected: Bool
	// New keeps its muted text on hover; Settings darkens.
	var tintsOnHover = true
	let action: () -> Void
	@ViewBuilder let label: Label

	var body: some View {
		Button(action: action) {
			HStack(spacing: 10) {
				label
				Spacer(minLength: 0)
			}
			.font(.system(size: 12))
			.padding(.leading, 7)
			.padding(.trailing, 8)
			.frame(height: SidebarRow.height)
			.contentShape(Rectangle())
		}
		.buttonStyle(RowButtonStyle(isSelected: isSelected, cornerRadius: SidebarRow.cornerRadius, tintsOnHover: tintsOnHover))
	}
}

// A profile row (36 points; the active one tinted with its color) and,
// expanded, its tabs and New tab, 32 points each and 4 apart: Aside's
// 36-point rows. Profiles are 4 apart. It takes what it shows as values
// and reads nothing of the model in its body, so a change to another
// profile, or to a tab's title, runs only the section and the row it
// concerns (`==`, `.equatable()`). Its actions go through the controller.
private struct ProfileSection: View, Equatable {
	let controller: BrowserController
	let profile: BrowserProfile
	let isActive: Bool
	let isExpanded: Bool
	let proxyStatus: BrowserModel.ProxyStatus?
	// The selected tab, when it is one of this profile's and shows.
	let selectedTabID: String?
	// Which of its tabs have a live page.
	let loadedTabIDs: Set<String>
	let reorder: VerticalReorder
	let onEdit: () -> Void

	// Its tabs reorder by dragging, as the Electron app's did.
	@State private var tabReorder = VerticalReorder(space: "pane.sidebar.tabs", rowHeight: 32, spacing: 4)
	@State private var confirmingDelete = false

	// The controller, the reorder, and the edit action are the same for the
	// section's life.
	nonisolated static func == (lhs: Self, rhs: Self) -> Bool {
		lhs.profile == rhs.profile
			&& lhs.isActive == rhs.isActive
			&& lhs.isExpanded == rhs.isExpanded
			&& lhs.proxyStatus == rhs.proxyStatus
			&& lhs.selectedTabID == rhs.selectedTabID
			&& lhs.loadedTabIDs == rhs.loadedTabIDs
	}

	var body: some View {
		VStack(alignment: .leading, spacing: 4) {
			header

			if isExpanded {
				VStack(alignment: .leading, spacing: 4) {
					VStack(alignment: .leading, spacing: 4) {
						ForEach(tabReorder.order(profile.tabs.map(\.id)), id: \.self) { id in
							if let tab = profile.tabs.first(where: { $0.id == id }) {
								TabRow(
									controller: controller,
									tab: tab,
									isActive: tab.id == selectedTabID,
									isLoaded: loadedTabIDs.contains(tab.id),
									profileID: profile.id,
									reorder: tabReorder
								)
								.equatable()
								.reorderRow(id, in: tabReorder)
								// What the sidebar scrolls into view for the
								// tab: its row and a fade's height around it.
								.background {
									Color.clear
										.frame(height: SidebarRow.height + 2 * ScrollFade.height)
										.id(RevealTarget(tabID: id))
								}
							}
						}
					}
					.reorderList(tabReorder) { id in
						if let tab = profile.tabs.first(where: { $0.id == id }) {
							HStack(spacing: 10) {
								FaviconView(tabID: tab.id, dataURL: tab.favicon)
								Text(tab.title.isEmpty ? "Loading..." : tab.title)
									.font(.system(size: 12))
									.lineLimit(1)
									.frame(maxWidth: .infinity, alignment: .leading)
							}
							.padding(.leading, 7)
							.padding(.trailing, 8)
							.frame(height: SidebarRow.height)
							.foregroundStyle(Palette.accentForeground)
							.dragCopy()
						}
					}

					// mt-1 against the last tab's 4px margin: CSS collapses
					// them to one 4px gap.
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
		let color = profile.color.color

		return Button(action: toggle) {
			ProfileHeaderContent(
				profile: profile,
				isActive: isActive,
				proxyStatus: proxyStatus,
				showsCount: !isExpanded
			)
			.padding(.horizontal, 8)
			.frame(height: 36)
			.foregroundStyle(isActive ? Palette.accentForeground : Palette.mutedForeground)
			.background {
				Squircle(cornerRadius: SidebarRow.cornerRadius)
					.fill(isActive ? color.opacity(0.04) : .clear)
			}
			// In the label, as the bar's morph: see ProfileHeaderContent.
			.animation(Motion.morph, value: isActive)
			.contentShape(Rectangle())
		}
		// Its text follows the profile, not the hover (the label's own).
		.buttonStyle(RowButtonStyle(isSelected: isActive, selectedFill: .clear, cornerRadius: SidebarRow.cornerRadius, tintsOnHover: false))
		// The Electron app's menu, with its lucide icons.
		.contextMenu {
			Button(action: onEdit) {
				MenuLabel("Edit profile", icon: .pencil)
			}
			Toggle(isOn: Binding(
				get: { profile.keepLoaded },
				set: { [controller, id = profile.id] value in controller.setKeepLoaded(id, value) }
			)) {
				MenuLabel("Keep loaded", icon: .timerOff)
			}
			Divider()
			Button(role: .destructive) { confirmingDelete = true } label: {
				MenuLabel("Delete profile", icon: .trash2)
			}
		}
		.simultaneousGesture(reorder.gesture(profile.id, ids: { [controller] in controller.model.profiles.map(\.id) }) { [controller] from, to in
			controller.moveProfile(from: from, to: to)
		})
		.accessibilityIdentifier("pane.profile.\(profile.id)")
		// The label takes the name alone; what the row shows besides goes in
		// its value, and the active profile reads as selected.
		.accessibilityLabel(profile.name)
		.accessibilityValue(accessibilityValue)
		.accessibilityAddTraits(isActive ? .isSelected : [])
	}

	// "3 tabs, collapsed, proxy connected".
	private var accessibilityValue: String {
		let count = profile.tabs.count
		var parts = ["\(count) \(count == 1 ? "tab" : "tabs")", isExpanded ? "expanded" : "collapsed"]

		if let proxyStatus {
			parts.append("proxy \(proxyStatus.rawValue)")
		}

		return parts.joined(separator: ", ")
	}

	private func toggle() {
		guard !reorder.isDragging else {
			return
		}

		controller.toggleProfile(profile.id)
	}
}

// A profile's header: its color (the active one's bar grown into a 16-point
// square, the favicons' size, rounding off as it widens, with the name moving
// along: DESIGN.md › Motion, an indicator morphs), the name, the proxy's dot,
// and the tab count while collapsed.
private struct ProfileHeaderContent: View {
	let profile: BrowserProfile
	let isActive: Bool
	let proxyStatus: BrowserModel.ProxyStatus?
	let showsCount: Bool

	var body: some View {
		HStack(spacing: 8) {
			Squircle(cornerRadius: isActive ? 4 : 2)
				.fill(profile.color.color)
				.frame(width: isActive ? 16 : 4, height: 16)

			Text(profile.name)
				.font(.system(size: 12, weight: .medium))
				.lineLimit(1)
				.truncationMode(.tail)
				.frame(maxWidth: .infinity, alignment: .leading)

			if let proxyStatus {
				ProxyDot(status: proxyStatus)
			}

			if showsCount && !profile.tabs.isEmpty {
				Text("\(profile.tabs.count)")
					.font(.system(size: 12))
					.foregroundStyle(Palette.mutedForeground)
			}
		}
		// Here, inside the button's label, and not on the button: a click on
		// the header (closing the active profile) ends the button's press in
		// the same update, and the press drops an animation set outside the
		// label, so the bar snapped back thin.
		.animation(Motion.morph, value: isActive)
	}
}

// The copy of a dragged row that rides the pointer: on the card's fill,
// rounded as the rows are, lifted by a soft shadow.
private extension View {
	func dragCopy() -> some View {
		frame(maxWidth: .infinity)
			.background(Palette.card, in: Squircle(cornerRadius: SidebarRow.cornerRadius))
			.overlay {
				Squircle(cornerRadius: SidebarRow.cornerRadius)
					.strokeBorder(Palette.border, lineWidth: 1)
			}
			.shadow(color: .black.opacity(0.12), radius: 8, y: 4)
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
		case .testing: Palette.warning
		case .connected: Palette.success
		case .failed: Palette.failure
		}
	}
}

// A 32-point row at 12, spaced as Aside's tabs: the 16-point favicon (or a
// globe) 7 in, the title 10 after it, and a close X on hover. Selected is
// foreground/5; unloaded tabs are at 60%. As its section, it takes what it
// shows as values, so selecting another tab runs only the two rows whose
// selection changed.
private struct TabRow: View, Equatable {
	let controller: BrowserController
	let tab: PaneKit.Tab
	let isActive: Bool
	let isLoaded: Bool
	let profileID: String
	let reorder: VerticalReorder

	nonisolated static func == (lhs: Self, rhs: Self) -> Bool {
		lhs.tab == rhs.tab && lhs.isActive == rhs.isActive && lhs.isLoaded == rhs.isLoaded && lhs.profileID == rhs.profileID
	}

	var body: some View {
		Button { unlessDragging { controller.select(tabID: tab.id) } } label: {
			HStack(spacing: 10) {
				FaviconView(tabID: tab.id, dataURL: tab.favicon)

				Text(tab.title.isEmpty ? "Loading..." : tab.title)
					.font(.system(size: 12))
					.lineLimit(1)
					.truncationMode(.tail)
					.frame(maxWidth: .infinity, alignment: .leading)

				TabCloseButton { unlessDragging { controller.closeTab(tab.id) } }
			}
			.padding(.leading, 7)
			.padding(.trailing, 8)
			// The row's content dims, not its fill.
			.opacity(isLoaded || isActive ? 1 : 0.6)
			.frame(height: SidebarRow.height)
			.contentShape(Rectangle())
		}
		.buttonStyle(RowButtonStyle(isSelected: isActive, cornerRadius: SidebarRow.cornerRadius))
		.contextMenu {
			Button("Close tab") { controller.closeTab(tab.id) }
		}
		.simultaneousGesture(reorder.gesture(tab.id, ids: { [controller, profileID] in controller.model.profile(profileID)?.tabs.map(\.id) ?? [] }) { [controller, profileID] from, to in
			controller.moveTab(inProfile: profileID, from: from, to: to)
		})
		.accessibilityIdentifier("pane.tab.\(tab.id)")
		.accessibilityLabel(tab.title.isEmpty ? "Loading" : tab.title)
		.accessibilityAddTraits(isActive ? .isSelected : [])
	}

	// A release on the row, or its X, after a drag ends the drag alone.
	private func unlessDragging(_ action: () -> Void) {
		if !reorder.isDragging {
			action()
		}
	}
}

// A tab's close X, in the row's text color, shown while the pointer is over
// the row: it fades with the row's fill.
private struct TabCloseButton: View {
	let action: () -> Void

	@Environment(\.isRowHovered) private var isRowHovered

	var body: some View {
		Button(action: action) {
			LucideIcon(.x, size: 14)
		}
		.buttonStyle(.plain)
		.opacity(isRowHovered ? 1 : 0)
		.accessibilityLabel("Close tab")
	}
}

// "+ New tab", a tab row's size, muted until hovered.
private struct NewTabRow: View {
	let action: () -> Void

	var body: some View {
		Button(action: action) {
			HStack(spacing: 10) {
				PlusMark()
				Text("New tab")
					.font(.system(size: 12))
				Spacer(minLength: 0)
			}
			.padding(.leading, 7)
			.padding(.trailing, 8)
			.frame(height: SidebarRow.height)
			.contentShape(Rectangle())
		}
		.buttonStyle(RowButtonStyle(cornerRadius: SidebarRow.cornerRadius))
	}
}

// A tab's place to scroll to, apart from its row's identity.
private struct RevealTarget: Hashable {
	let tabID: String
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
