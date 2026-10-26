import PaneKit
import SwiftUI

// The Ctrl+Tab switcher: recently used tabs grouped under their profile. The
// highlight jumps straight to the selection (DESIGN.md: selection is instant).
struct TabSwitcherView: View {
	let switcher: WindowState.Switcher
	let profiles: [BrowserProfile]

	var body: some View {
		VStack(alignment: .leading, spacing: 8) {
			ForEach(Array(switcher.groups.enumerated()), id: \.element.id) { groupIndex, group in
				VStack(alignment: .leading, spacing: 2) {
					HStack(spacing: 6) {
						Circle().fill(group.color.color).frame(width: 6, height: 6)
						Text(group.name)
							.font(.system(size: 11, weight: .semibold))
							.foregroundStyle(.secondary)
					}
					.padding(.horizontal, 8)
					.padding(.bottom, 2)

					ForEach(Array(group.tabs.enumerated()), id: \.element.id) { tabIndex, tab in
						let index = TabSwitcher.groupOffsets(switcher.groups)[groupIndex] + tabIndex
						HStack(spacing: 8) {
							FaviconView(dataURL: tab.favicon ?? "")
							Text(tab.title)
								.font(.system(size: 12))
								.lineLimit(1)
								.truncationMode(.tail)
							Spacer(minLength: 0)
						}
						.padding(.horizontal, 8)
						.padding(.vertical, 5)
						.padding(.leading, 12)
						.background(
							RoundedRectangle(cornerRadius: 6, style: .continuous)
								.fill(index == switcher.selectedIndex ? Color.accentColor.opacity(0.18) : .clear)
						)
						.accessibilityIdentifier("pane.switcher.tab.\(tab.id)")
						.accessibilityAddTraits(index == switcher.selectedIndex ? .isSelected : [])
					}
				}
			}
		}
		.padding(10)
		.frame(width: 360)
		.background(.regularMaterial, in: RoundedRectangle(cornerRadius: 12, style: .continuous))
		.shadow(color: .black.opacity(0.2), radius: 18, y: 6)
		.accessibilityElement(children: .contain)
		.accessibilityLabel("Tab switcher")
		.accessibilityIdentifier("pane.switcher")
	}
}
