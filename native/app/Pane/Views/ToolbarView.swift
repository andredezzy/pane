import PaneKit
import SwiftUI

// Back, forward, reload or stop, and the address field of the selected tab.
struct ToolbarView: View {
	let controller: BrowserController

	@State private var address = ""
	@FocusState private var addressFocused: Bool

	private var model: BrowserModel { controller.model }
	private var tabID: String? { model.activeTabID }
	private var navigation: BrowserModel.NavigationState {
		tabID.flatMap { model.navigation[$0] } ?? .init()
	}
	private var isLoading: Bool { tabID.flatMap { model.loadingProgress[$0] } != nil }

	var body: some View {
		HStack(spacing: 4) {
			ToolbarButton(systemImage: "chevron.left", label: "Back", action: controller.goBack)
				.disabled(!navigation.canGoBack)
			ToolbarButton(systemImage: "chevron.right", label: "Forward", action: controller.goForward)
				.disabled(!navigation.canGoForward)
			ToolbarButton(
				systemImage: isLoading ? "xmark" : "arrow.clockwise",
				label: isLoading ? "Stop" : "Reload",
				action: isLoading ? controller.stop : controller.reload
			)
			.disabled(tabID == nil)

			TextField("Search or enter address", text: $address)
				.textFieldStyle(.plain)
				.font(.system(size: 12))
				.padding(.horizontal, 10)
				.frame(height: 26)
				.background(Color.primary.opacity(0.05), in: RoundedRectangle(cornerRadius: 7, style: .continuous))
				.focused($addressFocused)
				.onSubmit {
					controller.navigate(to: address)
					addressFocused = false
				}
				.onExitCommand {
					address = model.activeTab?.url ?? ""
					addressFocused = false
					controller.activePage?.focus()
				}
				.accessibilityIdentifier("pane.address")
		}
		.padding(.horizontal, 8)
		.frame(height: 39)
		.overlay(alignment: .bottom) {
			if let progress = tabID.flatMap({ model.loadingProgress[$0] }) {
				GeometryReader { geometry in
					Rectangle()
						.fill(Color.accentColor)
						.frame(width: geometry.size.width * max(0.05, progress), height: 2)
				}
				.frame(height: 2)
			}
		}
		.onChange(of: model.activeTab?.url, initial: true) { _, url in
			if !addressFocused {
				address = url ?? ""
			}
		}
		.onChange(of: controller.state.addressFocusRequest) {
			addressFocused = true
		}
	}
}

private struct ToolbarButton: View {
	let systemImage: String
	let label: String
	let action: () -> Void

	@Environment(\.isEnabled) private var isEnabled

	var body: some View {
		Button(action: action) {
			Image(systemName: systemImage)
				.font(.system(size: 12, weight: .medium))
				.frame(width: 26, height: 26)
				.contentShape(Rectangle())
		}
		.buttonStyle(.plain)
		.foregroundStyle(isEnabled ? .secondary : .tertiary)
		.hoverBackground(isSelected: false, cornerRadius: 6)
		.help(label)
		.accessibilityLabel(label)
	}
}
