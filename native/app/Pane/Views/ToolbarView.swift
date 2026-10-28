import PaneKit
import SwiftUI

// The selected tab's toolbar at the top of the content panel, at Aside's
// sizes: back, forward, and reload or stop (32-point circles), the address
// (the domain and the title at 12, the URL while editing, with the loading
// bar under it), extension actions, and the profile's chip.
struct ToolbarView: View {
	let controller: BrowserController

	// The address last put in the field, to tell the user's typing from it.
	@State private var shownAddress = ""
	@FocusState private var addressFocused: Bool

	// The page starts 43px down the panel (TOOLBAR_HEIGHT 51 less the 8px
	// gutter), a pixel under the toolbar's 42.
	static let height: CGFloat = 43

	private var model: BrowserModel { controller.model }
	private var tabID: String? { model.activeTabID }
	// The selected tab's load progress, nil while it is not loading.
	private var progress: Double? { tabID.flatMap { model.loadingProgress[$0] } }
	private var isLoading: Bool { progress != nil }
	private var navigation: BrowserModel.NavigationState { tabID.flatMap { model.navigation[$0] } ?? .init() }
	private var profile: BrowserProfile? { model.activeProfileID.flatMap(model.profile) }

	var body: some View {
		HStack(spacing: 4) {
			HStack(spacing: 2) {
				// Aside's chevrons: 1.5 points of stroke at 24. Each dims to
				// 40%, as the other icon buttons, while the tab has nowhere to
				// go that way, as Chrome's and Aside's do.
				NavigationButton(icon: .chevronLeft, size: 24, strokeWidth: 1.5, label: "Back", action: controller.goBack)
					.disabled(!navigation.canGoBack)
				NavigationButton(icon: .chevronRight, size: 24, strokeWidth: 1.5, label: "Forward", action: controller.goForward)
					.disabled(!navigation.canGoForward)
				// Its reload at 17, with the same 1.5 points.
				NavigationButton(
					icon: isLoading ? .x : .rotateCw,
					size: 17,
					strokeWidth: 2.1,
					label: isLoading ? "Stop" : "Reload",
					action: isLoading ? controller.stop : controller.reload
				)
				.accessibilityIdentifier("pane.toolbar.reload")
			}

			AddressField(
				text: Bindable(controller.state).address,
				title: model.activeTab?.title ?? "",
				focused: $addressFocused,
				tabID: tabID,
				progress: progress,
				loadingSince: tabID.flatMap { model.loadingSince[$0] },
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

			ExtensionActionsView(extensions: controller.extensions, hasProfile: model.activeProfileID != nil)

			if let profile {
				ProfileChip(profile: profile)
			}
		}
		.padding(.leading, 5)
		.padding(.trailing, 6)
		.frame(height: Self.height - 1)
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

// A 32-point circle, muted; hover fills with the accent color.
private struct NavigationButton: View {
	let icon: Lucide
	let size: CGFloat
	let strokeWidth: Double
	let label: String
	let action: () -> Void

	var body: some View {
		Button(action: action) {
			LucideIcon(icon, size: size, strokeWidth: strokeWidth)
		}
		.buttonStyle(.icon(size: 32))
		.help(label)
		.accessibilityLabel(label)
	}
}

// 32 points, rounded 9, no fill until hovered (foreground 3%). At rest it
// shows the page as Aside does: the domain, a divider, and the title, at 12.
// A click edits the URL, with a 1px ring, and selects all of it.
private struct AddressField: View {
	@Binding var text: String
	let title: String
	var focused: FocusState<Bool>.Binding
	let tabID: String?
	let progress: Double?
	let loadingSince: Date?
	let onSubmit: () -> Void
	let onExit: () -> Void

	@State private var isHovered = false

	var body: some View {
		TextField(
			"",
			text: $text,
			prompt: Text("Search or enter URL").foregroundStyle(Palette.mutedForeground)
		)
		// On the field alone: on the whole, it would name the label over it too.
		.accessibilityLabel("Address")
		.textFieldStyle(.plain)
		.font(.system(size: 12))
		// At rest the label shows instead of the URL; the field stays
		// under it, to take the click and be read.
		.foregroundStyle(focused.wrappedValue || label == nil ? Palette.foreground : .clear)
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
		.overlay(alignment: .leading) {
			if !focused.wrappedValue, let label {
				AddressLabel(domain: label.domain, title: label.title)
					.padding(.leading, 14)
					.padding(.trailing, 10)
					.allowsHitTesting(false)
			}
		}
		.frame(height: 32)
		.background(
			isHovered ? Palette.foreground.opacity(0.03) : .clear,
			in: Squircle(cornerRadius: 9)
		)
		.overlay {
			if focused.wrappedValue {
				Squircle(cornerRadius: 9)
					.strokeBorder(Palette.ring, lineWidth: 1)
			}
		}
		.overlay(alignment: .bottomLeading) {
			LoadingBar(tabID: tabID, progress: progress, since: loadingSince)
		}
		.clipShape(Squircle(cornerRadius: 9))
		.onHover { hovering in
			withAnimation(Motion.hover) { isHovered = hovering }
		}
		.accessibilityIdentifier("pane.address")
	}

	// The domain (without www.) and the page's title, for a web address;
	// any other text shows as it is.
	private var label: (domain: String, title: String)? {
		guard let url = URL(string: text), ["http", "https"].contains(url.scheme), var host = url.host(), !host.isEmpty else {
			return nil
		}

		if host.hasPrefix("www.") {
			host.removeFirst(4)
		}

		let title = self.title.trimmingCharacters(in: .whitespaces)

		return (host, title == text ? "" : title)
	}
}

// Aside's address at rest: the domain in the text color, a divider, and the
// title muted, all at 12.
private struct AddressLabel: View {
	let domain: String
	let title: String

	var body: some View {
		HStack(spacing: 12) {
			Text(domain)
				.foregroundStyle(Palette.foreground)
				.fixedSize()

			if !title.isEmpty {
				Rectangle()
					.fill(Palette.mutedForeground.opacity(0.8))
					.frame(width: 0.5, height: 18)

				Text(title)
					.foregroundStyle(Palette.mutedForeground)
					.truncationMode(.tail)
			}

			Spacer(minLength: 0)
		}
		.font(.system(size: 12))
		.lineLimit(1)
	}
}

// The Electron app's address progress: a 2px bar that eases to 90% over 8s
// while the page loads, then fills and fades out. It shows 5% at once, as
// Chrome's starts at 5% and Safari's at 10%, so a click answers in the next
// frame. It follows the selected tab: a tab coming forward shows its own
// state at once, as Chrome's, Aside's, and Safari's do, nothing for a loaded
// tab (not the last tab's finish) and, for a loading one, the bar where it
// would be had the tab been shown since its load began (not a start from 0,
// and never behind where it was).
private struct LoadingBar: View {
	let tabID: String?
	// The tab's load progress, nil while it is not loading, and when it began.
	let progress: Double?
	let since: Date?

	@State private var fraction: CGFloat = 0
	@State private var opacity: Double = 0
	@State private var run = 0

	private static let start: CGFloat = 0.05
	private static let ceiling: CGFloat = 0.9
	// Seconds from the start to the ceiling, on this curve.
	private static let trickle: Double = 8
	private static let curve = UnitCurve.bezier(startControlPoint: UnitPoint(x: 0.1, y: 0.05), endControlPoint: UnitPoint(x: 0, y: 1))

	private struct Shown: Equatable {
		let tabID: String?
		let isLoading: Bool
	}

	var body: some View {
		GeometryReader { geometry in
			Rectangle()
				.fill(Palette.progress)
				.frame(width: geometry.size.width * fraction, height: 2)
				.frame(maxHeight: .infinity, alignment: .bottom)
		}
		.opacity(opacity)
		.allowsHitTesting(false)
		.onChange(of: Shown(tabID: tabID, isLoading: progress != nil), initial: true) { old, new in
			if old.tabID != new.tabID || old == new {
				jump()
			} else {
				new.isLoading ? start() : complete()
			}
		}
	}

	// A load starts on the tab shown.
	private func start() {
		set(fraction: Self.start, opacity: 1)
		ease(Self.trickle, .timingCurve(0.1, 0.05, 0, 1, duration: Self.trickle))
	}

	// Another tab forward, or the bar first shown: the tab's own state, with
	// no animation, and the rest of the ease from there.
	private func jump() {
		guard let progress else {
			set(fraction: 0, opacity: 0)
			return
		}

		let along = min(max(since.map { Date().timeIntervalSince($0) } ?? 0, 0) / Self.trickle, 1)
		let curved = Self.start + (Self.ceiling - Self.start) * CGFloat(Self.curve.value(at: along))
		let remaining = Self.trickle * (1 - along)

		set(fraction: min(max(curved, CGFloat(progress)), Self.ceiling), opacity: 1)
		ease(remaining, .easeOut(duration: remaining))
	}

	private func set(fraction: CGFloat, opacity: Double) {
		run += 1

		var transaction = Transaction()
		transaction.disablesAnimations = true

		withTransaction(transaction) {
			self.fraction = fraction
			self.opacity = opacity
		}
	}

	// On to the ceiling, over `duration`. A turn later, so the jump to where
	// the bar starts is not part of it.
	private func ease(_ duration: Double, _ animation: Animation) {
		let current = run

		DispatchQueue.main.async {
			guard current == run, duration > 0 else {
				return
			}

			withAnimation(Motion.unlessReduced(animation)) {
				fraction = Self.ceiling
			}
		}
	}

	private func complete() {
		// A load done within a turn: the ease it scheduled does not begin.
		run += 1

		guard opacity > 0 else {
			return
		}

		withAnimation(Motion.unlessReduced(.easeOut(duration: 0.2))) {
			fraction = 1
		}

		withAnimation(Motion.unlessReduced(.easeOut(duration: 0.2).delay(0.2))) {
			opacity = 0
		}
	}
}

// The profile's name in its color: a 28-point capsule at 12 medium, as
// Aside's toolbar button, with an 8% fill and a 30% border of the color.
private struct ProfileChip: View {
	let profile: BrowserProfile

	var body: some View {
		let color = profile.color.color

		Text(profile.name)
			.font(.system(size: 12, weight: .medium))
			.foregroundStyle(color)
			.lineLimit(1)
			.padding(.horizontal, 11)
			.frame(height: 28)
			.background {
				Capsule(style: .circular)
					.fill(color.opacity(0.08))
					.strokeBorder(color.opacity(0.3), lineWidth: 1)
			}
			.accessibilityIdentifier("pane.toolbar.profile")
	}
}
