import PaneKit
import SwiftUI

// The whole window, as the Electron app lays it out: the sidebar (260 points
// by default, as Aside's, and resizable from its edge), then the content
// panel (8px gutters, flush to the sidebar) holding the toolbar and the
// selected tab, or settings. Sheets, dialogs, the switcher, and find float
// over the whole window.
struct RootView: View {
	let controller: BrowserController

	private var model: BrowserModel { controller.model }
	private var state: WindowState { controller.state }
	private var extensions: ExtensionsController { controller.extensions }

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
			if extensions.menuOpen, let anchor = extensions.actionAnchors[ExtensionsController.menuAnchor] {
				Color.clear
					.contentShape(Rectangle())
					.onTapGesture(perform: extensions.closeMenu)

				let menuX = max(8, anchor.maxX - 300)

				ExtensionsMenu(controller: extensions)
					.padding(.leading, menuX)
					.padding(.top, anchor.maxY + 4)
					.frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)

				// An extension's more menu: beside the menu, over its edge by 10
				// points, level with the row, as Aside's.
				if let more = extensions.moreMenu, let item = extensions.active.first(where: { $0.id == more.extensionID }) {
					ExtensionMoreMenu(controller: extensions, item: item)
						.padding(.leading, max(8, menuX - 230))
						.padding(.top, more.row.minY)
						.frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
				}
			}

			// Adding an extension: Dia's dialog, centered on the scrim. Removing
			// one: Aside's popup under its toolbar button, else the puzzle's, and
			// centered where there is no toolbar (Settings). A click outside
			// either cancels.
			if let prompt = extensions.prompt, prompt.kind == .install {
				Palette.modalScrim
					.contentShape(Rectangle())
					.onTapGesture { extensions.answerPrompt(accepted: false) }

				ExtensionInstallDialog(controller: extensions, prompt: prompt)
			} else if let prompt = extensions.prompt {
				Color.clear
					.contentShape(Rectangle())
					.onTapGesture { extensions.answerPrompt(accepted: false) }

				if showsToolbar, let anchor = extensions.actionAnchors[prompt.extensionID] ?? extensions.actionAnchors[ExtensionsController.menuAnchor] {
					ExtensionPromptView(controller: extensions, prompt: prompt)
						.padding(.leading, max(8, anchor.maxX - 320))
						.padding(.top, anchor.maxY + 4)
						.frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
				} else {
					ExtensionPromptView(controller: extensions, prompt: prompt)
				}
			}

			if state.switcher.visible {
				Palette.sheetScrim
					.allowsHitTesting(false)

				TabSwitcherView(switcher: state.switcher, profiles: model.profiles)
			}

			// The overlays that animate out carry a z-index, the PIN screen's
			// over the rest: a view a ZStack removes drops to the back, so they
			// left unseen, under the window, in a single frame.
			if let target = state.profileSheet {
				Palette.sheetScrim
					.contentShape(Rectangle())
					.onTapGesture { present(nil) }
					.transition(.opacity)
					.zIndex(1)

				HStack {
					Spacer(minLength: 0)
					ProfileSheet(controller: controller, profileID: target.profileID) { present(nil) }
						.frame(width: 360)
						.padding(8)
				}
				.transition(.move(edge: .trailing).combined(with: .opacity))
				.zIndex(1)
			}

			if state.confirmingQuit {
				Palette.modalScrim
					.contentShape(Rectangle())
					.onTapGesture { controller.cancelQuit() }
					.transition(.opacity)
					.zIndex(1)

				QuitConfirmationView(cancel: controller.cancelQuit, quit: controller.quit)
					.transition(.opacity.combined(with: .scale(scale: 0.97)))
					.zIndex(1)
			}

			// The PIN screen, over everything; it slides up and back down.
			if let entry = controller.lock.entry {
				LockScreenView(lock: controller.lock, entry: entry, updater: controller.updater)
					.transition(.asymmetric(
						insertion: .move(edge: .bottom).animation(Motion.lockIn),
						removal: .move(edge: .bottom).animation(Motion.lockOut)
					))
					.zIndex(2)
			}
		}
		.animation(Motion.unlessReduced(.default), value: controller.lock.entry == nil)
		// The extensions menu belongs to the tab and the window it opened over.
		.onChange(of: model.activeTabID) { extensions.closeMenu() }
		.onChange(of: model.activeProfileID) { extensions.closeMenu() }
		.onChange(of: model.page) { extensions.closeMenu() }
		.onChange(of: controller.lock.entry == nil) { extensions.closeMenu() }
		.onReceive(NotificationCenter.default.publisher(for: NSWindow.didResignKeyNotification, object: controller.browserWindow.window)) { _ in
			extensions.closeMenu()
		}
		.background(Palette.background)
		.ignoresSafeArea()
		.overlay(alignment: .top) {
			if let hint = state.exclusiveAccessHint {
				ExclusiveAccessHint(text: hint)
			}
		}
	}

	// The content panel: m-2 ml-0, rounded 18, the panel shadow.
	private var content: some View {
		let radius: CGFloat = fullscreen ? 0 : 18

		return VStack(spacing: 0) {
			if showsToolbar {
				ToolbarView(controller: controller)
			}

			ZStack {
				// The page's layer rounds the panel's corners itself: a clip
				// over it made SwiftUI mask the page, composited each frame.
				PageHostRepresentable(host: controller.pageHost, cornerRadius: radius * Squircle.circularEquivalent)

				// What SwiftUI draws over the page takes the panel's corners:
				// the bottom ones, and the top ones too without the toolbar.
				Group {
					if model.page == .settings {
						SettingsView(controller: controller)
					} else if model.page == .extensions {
						ExtensionsPage(model: model, controller: extensions)
					} else if model.activeTabID == nil {
						EmptyStateView()
					} else if let tabID = model.activeTabID, model.crashedTabIDs.contains(tabID) {
						SadTabView { controller.reloadCrashed(tabID) }
					}
				}
				.clipShape(PanelCorners(radius: radius, top: showsToolbar ? ToolbarView.height : 0))
			}
		}
		.surfaceBackground(cornerRadius: radius, ring: fullscreen ? .clear : Palette.border, shadow: fullscreen ? .clear : PanelShadow.color)
		.padding([.top, .trailing, .bottom], fullscreen ? 0 : 8)
	}

	// Sheets slide in from the right; their scrim fades (DESIGN.md: enter and
	// exit animate).
	private func present(_ target: WindowState.ProfileSheetTarget?) {
		withAnimation(Motion.sheet) {
			state.profileSheet = target
		}
	}
}

// The content panel's squircle, for a view inside it `top` points under its
// top edge (below the toolbar): the view's corners round where the panel's
// do, and its top ones meet the toolbar square.
private struct PanelCorners: Shape {
	let radius: CGFloat
	let top: CGFloat

	func path(in rect: CGRect) -> Path {
		Squircle(cornerRadius: radius).path(in: CGRect(x: rect.minX, y: rect.minY - top, width: rect.width, height: rect.height + top))
	}
}

// The engine's page view lives in an AppKit host the controller fills. Its
// layer takes the panel's corners as a circle (Squircle.circularEquivalent),
// square in full screen.
struct PageHostRepresentable: NSViewRepresentable {
	let host: PageHostView
	let cornerRadius: CGFloat

	func makeNSView(context: Context) -> PageHostView {
		host
	}

	func updateNSView(_ nsView: PageHostView, context: Context) {
		nsView.layer?.cornerRadius = cornerRadius
	}
}
