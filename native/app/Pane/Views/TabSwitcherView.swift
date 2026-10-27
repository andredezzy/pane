import PaneKit
import SwiftUI

// The Ctrl+Tab switcher, as the Electron app's: recently used tabs grouped
// under their profile, in a 320px panel over a black 40% scrim. The selected
// row takes 14% of the profile's color with a 40% ring, and jumps straight to
// the selection (DESIGN.md: selection is instant).
struct TabSwitcherView: View {
	let switcher: WindowState.Switcher
	let profiles: [BrowserProfile]

	var body: some View {
		VStack(alignment: .leading, spacing: 8) {
			ForEach(Array(switcher.groups.enumerated()), id: \.element.id) { groupIndex, group in
				let color = group.color.color

				VStack(alignment: .leading, spacing: 0) {
					HStack(spacing: 8) {
						Circle().fill(color).frame(width: 8, height: 8)
						Text(group.name)
							.font(.system(size: 11, weight: .medium))
							.foregroundStyle(Palette.mutedForeground)
							.lineLimit(1)
							.lineBox(16.5)
					}
					.padding(.horizontal, 12)
					.padding(.top, 6)
					.padding(.bottom, 4)

					ForEach(Array(group.tabs.enumerated()), id: \.element.id) { tabIndex, tab in
						let index = TabSwitcher.groupOffsets(switcher.groups)[groupIndex] + tabIndex
						let isSelected = index == switcher.selectedIndex

						HStack(spacing: 10) {
							SwitcherFavicon(dataURL: tab.favicon ?? "")
							Text(tab.title)
								.font(.system(size: 14))
								.foregroundStyle(Palette.foreground.opacity(0.8))
								.lineLimit(1)
								.truncationMode(.tail)
								.lineBox(20)
							Spacer(minLength: 0)
						}
						.padding(.leading, 28)
						.padding(.trailing, 12)
						.padding(.vertical, 10)
						.background {
							if isSelected {
								Squircle(cornerRadius: 15)
									.fill(color.opacity(0x24 / 255))
									.strokeBorder(color.opacity(0x66 / 255), lineWidth: 1)
							}
						}
						.accessibilityIdentifier("pane.switcher.tab.\(tab.id)")
						.accessibilityAddTraits(isSelected ? .isSelected : [])
					}
				}
			}
		}
		.padding(10)
		.frame(width: 320)
		.popoverSurface(cornerRadius: 22)
		.accessibilityElement(children: .contain)
		.accessibilityLabel("Tab switcher")
		.accessibilityIdentifier("pane.switcher")
	}
}

// 16px, rounded-sm; without a favicon, a foreground/10 square.
private struct SwitcherFavicon: View {
	let dataURL: String

	var body: some View {
		if let image = FaviconView.image(from: dataURL) {
			Image(nsImage: image)
				.resizable()
				.interpolation(.high)
				.frame(width: 16, height: 16)
				.clipShape(Squircle(cornerRadius: 4))
		} else {
			Squircle(cornerRadius: 4)
				.fill(Palette.foreground.opacity(0.1))
				.frame(width: 16, height: 16)
		}
	}
}
