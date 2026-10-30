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
						onNewProfile: { controller.presentProfileSheet(.new) },
						onEditProfile: { controller.presentProfileSheet(.edit($0)) }
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

			// A page's dialog, where its alert sheet sat; it fades out over
			// the window, not under it (a z-index), and under the overlays
			// that follow, as the keys go (KeyRouter).
			if let dialog = controller.dialogs.dialog {
				AlertPlacement {
					DialogView(dialog: dialog, presenter: controller.dialogs)
				}
				.transition(.opacity.combined(with: .scale(scale: 0.97)))
				.zIndex(0.5)
			}

			Group {
				extensionsOverlays
			}
			.zIndex(0.6)

			// The overlays that animate out carry a z-index, the PIN screen's
			// over the rest: a view a ZStack removes drops to the back, so they
			// left unseen, under the window, in a single frame.
			if let target = state.profileSheet {
				Palette.sheetScrim
					.contentShape(Rectangle())
					.onTapGesture { controller.presentProfileSheet(nil) }
					.transition(.opacity)
					.zIndex(1)

				HStack {
					Spacer(minLength: 0)
					ProfileSheet(profile: target.profileID.flatMap(model.profile), save: { draft in
						if let id = target.profileID {
							controller.saveProfile(id, draft)
						} else {
							controller.createProfile(draft)
						}
					}) {
						controller.presentProfileSheet(nil)
					}
						.frame(width: 360)
						.padding(8)
				}
				.transition(.move(edge: .trailing).combined(with: .opacity))
				.zIndex(1)
			}

			if state.choosingAppIcon {
				Palette.modalScrim
					.contentShape(Rectangle())
					.onTapGesture { controller.closeAppIconPicker() }
					.transition(.opacity)
					.zIndex(1)

				AppIconPicker(selection: model.settings.appIcon, select: { icon in
					controller.changeSettings { $0.appIcon = icon }
				}, done: controller.closeAppIconPicker)
					.transition(.opacity.combined(with: .scale(scale: 0.97)))
					.zIndex(1)
			}

			// The Extensions page's Keyboard shortcuts. A click outside it stops a
			// recording first, then closes it.
			if let dialog = extensions.shortcuts {
				Palette.modalScrim
					.contentShape(Rectangle())
					.onTapGesture {
						dialog.recording == nil ? extensions.closeShortcuts() : extensions.cancelRecording()
					}
					.transition(.opacity)
					.zIndex(1)

				ExtensionShortcutsDialog(
					controller: extensions,
					dialog: dialog,
					profileName: model.profile(dialog.profileID)?.name ?? "This profile"
				)
				.transition(.opacity.combined(with: .scale(scale: 0.97)))
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
		.onChange(of: model.page) {
			extensions.closeMenu()
			extensions.closeShortcuts()
		}
		.onChange(of: controller.lock.entry == nil) { extensions.closeMenu() }
		.onReceive(NotificationCenter.default.publisher(for: NSWindow.didResignKeyNotification, object: controller.browserWindow.window)) { _ in
			extensions.closeMenu()
			// As Chrome's field ends one when it loses focus.
			extensions.cancelRecording()
		}
		.background(Palette.background)
		.ignoresSafeArea()
		.overlay(alignment: .top) {
			if let hint = state.exclusiveAccessHint {
				ExclusiveAccessHint(text: hint)
			}
		}
	}

	// The extensions menu, its popups, and the switcher: over a page's
	// dialog.
	@ViewBuilder
	private var extensionsOverlays: some View {
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
						SettingsView(settings: model.settings, lock: controller.lock, updater: controller.updater, change: controller.changeSettings, chooseAppIcon: controller.openAppIconPicker)
					} else if model.page == .extensions {
						ExtensionsPage(model: model, controller: extensions)
					} else if model.activeTabID == nil {
						EmptyStateView()
					} else if let tabID = model.activeTabID, model.crashedTabIDs.contains(tabID) {
						SadTabView { controller.reloadCrashed(tabID) }
					}
				}
				.clipShape(PanelCorners(radius: radius, top: showsToolbar ? ToolbarView.height : 0))

				// No click reaches the page under its dialog.
				if controller.dialogs.dialog != nil {
					Color.clear
						.contentShape(Rectangle())
				}
			}
		}
		.surfaceBackground(cornerRadius: radius, ring: fullscreen ? .clear : Palette.border, shadow: fullscreen ? .clear : PanelShadow.color)
		.padding([.top, .trailing, .bottom], fullscreen ? 0 : 8)
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

// Where an alert sheet sat on the window: centered across it, its top at
// three eighths of its height (Paper: 300 of 800), raised as far as it must
// to fit. Below that line it has the rest of the window; a longer message
// scrolls.
private struct AlertPlacement: Layout {
	func sizeThatFits(proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) -> CGSize {
		proposal.replacingUnspecifiedDimensions()
	}

	func placeSubviews(in bounds: CGRect, proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) {
		let line = (bounds.height * 3 / 8).rounded()

		for subview in subviews {
			let size = subview.sizeThatFits(ProposedViewSize(width: bounds.width, height: bounds.height - line - 16))
			let top = max(8, min(line, bounds.height - size.height - 16))
			subview.place(at: CGPoint(x: bounds.midX, y: bounds.minY + top), anchor: .top, proposal: ProposedViewSize(size))
		}
	}
}
