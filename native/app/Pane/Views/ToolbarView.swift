import PaneKit
import SwiftUI

// The selected tab's toolbar at the top of the content panel, as in the
// Electron app: back, forward, and reload or stop (26px), the address (30px,
// with the loading bar under it), extension actions, and the profile's chip.
struct ToolbarView: View {
	let controller: BrowserController

	// The address last put in the field, to tell the user's typing from it.
	@State private var shownAddress = ""
	@FocusState private var addressFocused: Bool

	private var model: BrowserModel { controller.model }
	private var tabID: String? { model.activeTabID }
	private var isLoading: Bool { tabID.flatMap { model.loadingProgress[$0] } != nil }
	private var profile: BrowserProfile? { model.activeProfileID.flatMap(model.profile) }

	var body: some View {
		HStack(spacing: 6) {
			HStack(spacing: 2) {
				NavigationButton(icon: .arrowLeft, size: 14, label: "Back", action: controller.goBack)
				NavigationButton(icon: .arrowRight, size: 14, label: "Forward", action: controller.goForward)
				NavigationButton(
					icon: isLoading ? .x : .rotateCw,
					size: 12,
					label: isLoading ? "Stop" : "Reload",
					action: isLoading ? controller.stop : controller.reload
				)
				.accessibilityIdentifier("pane.toolbar.reload")
			}

			AddressField(
				text: Bindable(controller.state).address,
				focused: $addressFocused,
				isLoading: isLoading,
				onSubmit: {
					controller.navigate(to: controller.state.address)
					addressFocused = false
				},
				onExit: {
					showAddress(model.activeTab?.url)
					addressFocused = false
					controller.activePage?.focus()
				}
			)

			ExtensionActionsView(controller: controller)

			if let profile {
				ProfileChip(profile: profile)
			}
		}
		.padding(6)
		// The page starts 43px down the panel (TOOLBAR_HEIGHT 51 less the 8px
		// gutter), a pixel under the toolbar.
		.padding(.bottom, 1)
		// Another tab shows its own address. The same tab's new address
		// replaces the text unless the user is typing over it.
		.onChange(of: tabID) {
			showAddress(model.activeTab?.url)
		}
		.onChange(of: model.activeTab?.url, initial: true) { _, url in
			if !addressFocused || controller.state.address == shownAddress {
				showAddress(url)
			}
		}
		.onChange(of: controller.state.addressFocusRequest) {
			addressFocused = true
		}
	}

	private func showAddress(_ url: String?) {
		controller.state.address = url ?? ""
		shownAddress = controller.state.address
	}
}

// 26 × 26, rounded 4, muted; hover fills with the accent color.
private struct NavigationButton: View {
	let icon: Lucide
	let size: CGFloat
	let label: String
	let action: () -> Void

	@State private var isHovered = false
	@Environment(\.accessibilityReduceMotion) private var reduceMotion

	var body: some View {
		Button(action: action) {
			LucideIcon(icon, size: size)
				.frame(width: 26, height: 26)
				.contentShape(Rectangle())
		}
		.buttonStyle(.plain)
		.foregroundStyle(isHovered ? Palette.accentForeground : Palette.mutedForeground)
		.background(isHovered ? Palette.accent : .clear, in: RoundedRectangle(cornerRadius: 4, style: .circular))
		.onHover { hovering in
			withAnimation(reduceMotion ? nil : .easeOut(duration: 0.15)) { isHovered = hovering }
		}
		.help(label)
		.accessibilityLabel(label)
	}
}

// 30px, rounded 5, px-2.5 text-xs, no fill until hovered (foreground 3%); a
// 1px ring when focused, which selects the whole address.
private struct AddressField: View {
	@Binding var text: String
	var focused: FocusState<Bool>.Binding
	let isLoading: Bool
	let onSubmit: () -> Void
	let onExit: () -> Void

	@State private var isHovered = false
	@Environment(\.accessibilityReduceMotion) private var reduceMotion

	var body: some View {
		TextField(
			"",
			text: $text,
			prompt: Text("Search or enter URL").foregroundStyle(Palette.mutedForeground)
		)
		.textFieldStyle(.plain)
		.font(.system(size: 12))
		.foregroundStyle(Palette.foreground)
		.focused(focused)
		.onSubmit(onSubmit)
		.onExitCommand(perform: onExit)
		.onChange(of: focused.wrappedValue) { _, isFocused in
			if isFocused {
				// As the Electron app's input.select() on focus.
				DispatchQueue.main.async {
					NSApp.sendAction(#selector(NSText.selectAll(_:)), to: nil, from: nil)
				}
			}
		}
		.padding(.horizontal, 10)
		.frame(height: 30)
		.background(
			isHovered ? Palette.foreground.opacity(0.03) : .clear,
			in: RoundedRectangle(cornerRadius: 5, style: .circular)
		)
		.overlay {
			if focused.wrappedValue {
				RoundedRectangle(cornerRadius: 5, style: .circular)
					.strokeBorder(Palette.ring, lineWidth: 1)
			}
		}
		.overlay(alignment: .bottomLeading) {
			LoadingBar(isLoading: isLoading)
		}
		.clipShape(RoundedRectangle(cornerRadius: 5, style: .circular))
		.onHover { hovering in
			withAnimation(reduceMotion ? nil : .easeOut(duration: 0.15)) { isHovered = hovering }
		}
		.accessibilityIdentifier("pane.address")
	}
}

// The Electron app's address progress: a 2px bar that eases to 90% over 8s
// while the page loads, then fills and fades out.
private struct LoadingBar: View {
	let isLoading: Bool

	@State private var fraction: CGFloat = 0
	@State private var opacity: Double = 0
	@State private var run = 0
	@Environment(\.accessibilityReduceMotion) private var reduceMotion

	var body: some View {
		GeometryReader { geometry in
			Rectangle()
				.fill(Palette.progress)
				.frame(width: geometry.size.width * fraction, height: 2)
				.frame(maxHeight: .infinity, alignment: .bottom)
		}
		.opacity(opacity)
		.allowsHitTesting(false)
		.onChange(of: isLoading, initial: true) { _, loading in
			loading ? start() : complete()
		}
	}

	private func start() {
		run += 1
		fraction = 0
		opacity = 1

		let current = run
		DispatchQueue.main.async {
			guard current == run else {
				return
			}

			withAnimation(reduceMotion ? nil : .timingCurve(0.1, 0.05, 0, 1, duration: 8)) {
				fraction = 0.9
			}
		}
	}

	private func complete() {
		guard opacity > 0 else {
			return
		}

		withAnimation(reduceMotion ? nil : .easeOut(duration: 0.2)) {
			fraction = 1
		}

		withAnimation(reduceMotion ? nil : .easeOut(duration: 0.2).delay(0.2)) {
			opacity = 0
		}
	}
}

// The profile's name in its color: h-30, px-2.5, 11px medium, an 8% fill and
// a 30% border of the color.
private struct ProfileChip: View {
	let profile: BrowserProfile

	var body: some View {
		let color = profile.color.color

		Text(profile.name)
			.font(.system(size: 11, weight: .medium))
			.foregroundStyle(color)
			.lineLimit(1)
			// px-2.5 inside the 1px border.
			.padding(.horizontal, 11)
			.frame(height: 30)
			.background(color.opacity(0.08), in: RoundedRectangle(cornerRadius: 5, style: .circular))
			.overlay {
				RoundedRectangle(cornerRadius: 5, style: .circular)
					.strokeBorder(color.opacity(0.3), lineWidth: 1)
			}
			.accessibilityIdentifier("pane.toolbar.profile")
	}
}
