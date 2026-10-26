import PaneKit
import SwiftUI

// The whole window: sidebar, then the toolbar and the selected tab in a
// rounded card (or settings), with the find bar and switcher above.
struct RootView: View {
	let controller: BrowserController

	@State private var editingProfileID: String?
	@State private var creatingProfile = false

	private var model: BrowserModel { controller.model }
	private var state: WindowState { controller.state }

	var body: some View {
		HStack(spacing: 0) {
			SidebarView(
				controller: controller,
				onNewProfile: { creatingProfile = true },
				onEditProfile: { editingProfileID = $0 }
			)
			.frame(width: 220)

			VStack(spacing: 0) {
				if model.page == .browser {
					ToolbarView(controller: controller)
				} else {
					Color.clear.frame(height: 39)
				}

				ZStack(alignment: .topTrailing) {
					PageHostRepresentable(host: controller.pageHost)

					if model.page == .settings {
						SettingsView(controller: controller)
					} else if model.activeTabID == nil {
						EmptyStateView(hasProfiles: !model.profiles.isEmpty)
					}

					if state.find.visible && model.page == .browser {
						FindBar(controller: controller)
					}
				}
				.clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
				.shadow(color: .black.opacity(0.08), radius: 3, y: 1)
				.padding([.trailing, .bottom], 8)
			}
		}
		.overlay {
			if state.switcher.visible {
				TabSwitcherView(switcher: state.switcher, profiles: model.profiles)
			}
		}
		.sheet(isPresented: $creatingProfile) {
			ProfileSheet(controller: controller, profileID: nil) { creatingProfile = false }
		}
		.sheet(item: Binding(
			get: { editingProfileID.map(EditedProfile.init) },
			set: { editingProfileID = $0?.id }
		)) { edited in
			ProfileSheet(controller: controller, profileID: edited.id) { editingProfileID = nil }
		}
	}
}

private struct EditedProfile: Identifiable {
	let id: String
}

// The engine's page view lives in an AppKit host the controller fills.
struct PageHostRepresentable: NSViewRepresentable {
	let host: PageHostView

	func makeNSView(context: Context) -> PageHostView {
		host
	}

	func updateNSView(_ nsView: PageHostView, context: Context) {}
}
