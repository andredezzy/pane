import PaneKit
import SwiftUI

// The whole window, as the Electron app lays it out: the sidebar (260 points
// by default, as Aside's, and resizable from its edge), then the content
// panel (8px gutters, flush to the sidebar) holding the toolbar and the
// selected tab, or settings. Sheets, dialogs, the switcher, and find float
// over the whole window.
struct RootView: View {
	let controller: BrowserController

	@Environment(\.accessibilityReduceMotion) private var reduceMotion

	private var model: BrowserModel { controller.model }
	private var state: WindowState { controller.state }

	// A page in fullscreen fills the window, without the panel around it.
	private var fullscreen: Bool { state.pageFullscreen && model.page == .browser }
	private var showsToolbar: Bool { !fullscreen && model.page == .browser && model.activeTabID != nil }

	var body: some View {
		ZStack {
			// Under everything: the window moves wherever no control is.
			WindowDragArea()

			HStack(spacing: 0) {
				if !fullscreen {
					SidebarView(
						controller: controller,
						onNewProfile: { present(.new) },
						onEditProfile: { present(.edit($0)) }
					)
					.frame(width: state.sidebarWidth)
				}

				content
			}

			// The sidebar's edge: 8 points, 5 over the sidebar and 3 over the
			// panel, under every overlay.
			if !fullscreen {
				SidebarResizeHandle(
					width: state.sidebarWidth,
					resize: controller.resizeSidebar(to:),
					end: controller.endSidebarResize,
					reset: controller.resetSidebarWidth
				)
				.frame(width: 8)
				.padding(.leading, state.sidebarWidth - 5)
				.frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .leading)
			}

			if state.find.visible && model.page == .browser {
				FindOverlay(controller: controller)
			}

			// The extensions menu, its trailing edge on the puzzle's, 4 points
			// under it. A click anywhere else closes it, as a menu's would.
			if state.extensionsMenuOpen, let anchor = state.actionAnchors[WindowState.extensionsMenuAnchor] {
				Color.clear
					.contentShape(Rectangle())
					.onTapGesture(perform: controller.closeExtensionsMenu)

				let menuX = max(8, anchor.maxX - 300)

				ExtensionsMenu(controller: controller)
					.padding(.leading, menuX)
					.padding(.top, anchor.maxY + 4)
					.frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)

				// An extension's more menu: beside the menu, over its edge by 10
				// points, level with the row, as Aside's.
				if let more = state.extensionMoreMenu, let item = activeExtension(more.extensionID) {
					ExtensionMoreMenu(controller: controller, item: item)
						.padding(.leading, max(8, menuX - 230))
						.padding(.top, more.row.minY)
						.frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
				}
			}

			// Adding an extension: Dia's dialog, centered on the scrim. Removing
			// one: Aside's popup under its toolbar button, else the puzzle's, and
			// centered where there is no toolbar (Settings). A click outside
			// either cancels.
			if let prompt = state.extensionPrompt, prompt.kind == .install {
				Self.modalScrim
					.contentShape(Rectangle())
					.onTapGesture { controller.answerExtensionPrompt(accepted: false) }

				ExtensionInstallDialog(controller: controller, prompt: prompt)
			} else if let prompt = state.extensionPrompt {
				Color.clear
					.contentShape(Rectangle())
					.onTapGesture { controller.answerExtensionPrompt(accepted: false) }

				if showsToolbar, let anchor = state.actionAnchors[prompt.extensionID] ?? state.actionAnchors[WindowState.extensionsMenuAnchor] {
					ExtensionPromptView(controller: controller, prompt: prompt)
						.padding(.leading, max(8, anchor.maxX - 320))
						.padding(.top, anchor.maxY + 4)
						.frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
				} else {
					ExtensionPromptView(controller: controller, prompt: prompt)
				}
			}

			if state.switcher.visible {
				Palette.sheetScrim
					.allowsHitTesting(false)

				TabSwitcherView(switcher: state.switcher, profiles: model.profiles)
			}

			if let target = state.profileSheet {
				Palette.sheetScrim
					.contentShape(Rectangle())
					.onTapGesture { present(nil) }
					.transition(.opacity)

				HStack {
					Spacer(minLength: 0)
					ProfileSheet(controller: controller, profileID: target.profileID) { present(nil) }
						.frame(width: 360)
						.padding(8)
				}
				.transition(.move(edge: .trailing).combined(with: .opacity))
			}

			if state.confirmingQuit {
				Self.modalScrim
					.contentShape(Rectangle())
					.onTapGesture { controller.cancelQuit() }
					.transition(.opacity)

				QuitConfirmationView(cancel: controller.cancelQuit, quit: controller.quit)
					.transition(.opacity.combined(with: .scale(scale: 0.97)))
			}

			// The PIN screen, over everything; it slides up and back down.
			if let entry = controller.lock.entry {
				LockScreenView(lock: controller.lock, entry: entry, updater: controller.updater)
					.transition(.asymmetric(
						insertion: .move(edge: .bottom).animation(reduceMotion ? nil : .timingCurve(0.16, 1, 0.3, 1, duration: 0.4)),
						removal: .move(edge: .bottom).animation(reduceMotion ? nil : .easeIn(duration: 0.35))
					))
			}
		}
		.animation(reduceMotion ? nil : .default, value: controller.lock.entry == nil)
		// The extensions menu belongs to the tab and the window it opened over.
		.onChange(of: model.activeTabID) { controller.closeExtensionsMenu() }
		.onChange(of: model.activeProfileID) { controller.closeExtensionsMenu() }
		.onChange(of: model.page) { controller.closeExtensionsMenu() }
		.onChange(of: controller.lock.entry == nil) { controller.closeExtensionsMenu() }
		.onReceive(NotificationCenter.default.publisher(for: NSWindow.didResignKeyNotification, object: controller.browserWindow.window)) { _ in
			controller.closeExtensionsMenu()
		}
		.background(Palette.background)
		.ignoresSafeArea()
		.overlay(alignment: .top) {
			if let hint = state.exclusiveAccessHint {
				ExclusiveAccessHint(text: hint)
			}
		}
	}

	private func activeExtension(_ extensionID: String) -> ExtensionInfo? {
		model.activeProfileID.flatMap { model.extensions[$0] }?.first { $0.id == extensionID }
	}

	// Quit Pane?'s and the install dialog's scrim: a light veil, as Aside's,
	// darker on dark.
	private static let modalScrim = Color(nsColor: NSColor(name: nil) { appearance in
		NSColor(white: 0, alpha: Palette.isDark(appearance) ? 0.45 : 0.14)
	})

	// The content panel: m-2 ml-0, rounded 18, the panel shadow.
	private var content: some View {
		VStack(spacing: 0) {
			if showsToolbar {
				ToolbarView(controller: controller)
			}

			ZStack {
				PageHostRepresentable(host: controller.pageHost)

				if model.page == .settings {
					SettingsView(controller: controller)
				} else if model.page == .extensions {
					ExtensionsPage(controller: controller)
				} else if model.activeTabID == nil {
					EmptyStateView()
				} else if let tabID = model.activeTabID, model.crashedTabIDs.contains(tabID) {
					SadTabView { controller.reloadCrashed(tabID) }
				}
			}
		}
		.clipShape(Squircle(cornerRadius: fullscreen ? 0 : 18))
		.surfaceBackground(cornerRadius: fullscreen ? 0 : 18, ring: fullscreen ? .clear : Palette.border, shadow: fullscreen ? .clear : PanelShadow.color)
		.padding([.top, .trailing, .bottom], fullscreen ? 0 : 8)
	}

	// Sheets slide in from the right; their scrim fades (DESIGN.md: enter and
	// exit animate).
	private func present(_ target: WindowState.ProfileSheetTarget?) {
		withAnimation(reduceMotion ? nil : .easeOut(duration: 0.2)) {
			state.profileSheet = target
		}
	}
}

// The engine's page view lives in an AppKit host the controller fills.
struct PageHostRepresentable: NSViewRepresentable {
	let host: PageHostView

	func makeNSView(context: Context) -> PageHostView {
		host
	}

	func updateNSView(_ nsView: PageHostView, context: Context) {}
}
