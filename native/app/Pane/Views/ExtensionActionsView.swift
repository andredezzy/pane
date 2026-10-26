import SwiftUI

// The active profile's extension actions in the toolbar: pinned ones as
// buttons with their badge, the rest in the extensions menu.
struct ExtensionActionsView: View {
	let controller: BrowserController

	private var model: BrowserModel { controller.model }
	private var profileID: String? { model.activeProfileID }
	private var extensions: [ExtensionInfo] {
		profileID.flatMap { model.extensions[$0] }?.filter { $0.enabled && $0.hasAction } ?? []
	}

	var body: some View {
		HStack(spacing: 5) {
			ForEach(extensions.filter(\.pinned)) { item in
				ActionButton(item: item) { controller.runExtensionAction(item.id) }
					.anchorPreference(item.id)
			}

			if !extensions.isEmpty {
				Menu {
					ForEach(extensions) { item in
						Button(item.name) { controller.runExtensionAction(item.id) }
					}

					Divider()

					ForEach(extensions) { item in
						Button(item.pinned ? "Unpin \(item.name)" : "Pin \(item.name)") {
							if let profileID {
								controller.setExtension(item.id, pinned: !item.pinned, inProfile: profileID)
							}
						}
					}

					Divider()

					Button("Manage Extensions…") { controller.model.page = .settings }
				} label: {
					LucideIcon(.puzzle, size: 14)
				}
				.menuStyle(.borderlessButton)
				.menuIndicator(.hidden)
				.frame(width: 28, height: 28)
				.foregroundStyle(Palette.mutedForeground)
				.help("Extensions")
				.accessibilityLabel("Extensions")
				.accessibilityIdentifier("pane.extensions")
				.anchorPreference(WindowState.extensionsMenuAnchor)
			}
		}
		.onPreferenceChange(ActionAnchorKey.self) { anchors in
			controller.state.actionAnchors = anchors
		}
	}
}

// The Electron app's action button (electron-chrome-extensions): 28px,
// rounded 4, the icon at 70%, a faint white wash on hover; no icon shows the
// name's first letter on gray. The badge is 12px, 9px text, bottom right.
private struct ActionButton: View {
	let item: ExtensionInfo
	let action: () -> Void

	@State private var isHovered = false

	var body: some View {
		Button(action: action) {
			Group {
				if let icon = item.icon {
					Image(nsImage: icon)
						.resizable()
						.interpolation(.high)
						.frame(width: 19.6, height: 19.6)
				} else {
					Text(item.name.prefix(1).uppercased())
						.font(.system(size: 11.2))
						.foregroundStyle(.white)
						.frame(width: 22.4, height: 22.4)
						.background(Color(rgb: 0x757575), in: RoundedRectangle(cornerRadius: 4, style: .circular))
				}
			}
			.frame(width: 28, height: 28)
			.background(isHovered ? Color.white.opacity(0.3) : .clear, in: RoundedRectangle(cornerRadius: 4, style: .circular))
			.overlay(alignment: .bottomTrailing) {
				if !item.badgeText.isEmpty {
					Text(item.badgeText)
						.font(.system(size: 9))
						.lineLimit(1)
						.foregroundStyle(Color(nsColor: item.badgeTextColor ?? .white))
						.padding(.horizontal, 2)
						.frame(height: 12)
						.background(Color(nsColor: item.badgeBackground ?? .systemRed), in: RoundedRectangle(cornerRadius: 2, style: .circular))
						.overlay {
							RoundedRectangle(cornerRadius: 2, style: .circular).stroke(Color(rgb: 0x444444), lineWidth: 1)
						}
						.padding(.bottom, 1)
				}
			}
			.contentShape(Rectangle())
		}
		.buttonStyle(.plain)
		.opacity(item.actionEnabled ? 1 : 0.4)
		.onHover { isHovered = $0 }
		.help(item.actionTitle.isEmpty ? item.name : item.actionTitle)
		.accessibilityLabel(item.actionTitle.isEmpty ? item.name : item.actionTitle)
		.accessibilityIdentifier("pane.extension.\(item.id)")
	}
}

// Each toolbar button's frame, keyed by extension ID, for popups to point at.
private struct ActionAnchorKey: PreferenceKey {
	static let defaultValue: [String: CGRect] = [:]

	static func reduce(value: inout [String: CGRect], nextValue: () -> [String: CGRect]) {
		value.merge(nextValue()) { $1 }
	}
}

private extension View {
	func anchorPreference(_ key: String) -> some View {
		background {
			GeometryReader { geometry in
				Color.clear.preference(key: ActionAnchorKey.self, value: [key: geometry.frame(in: .global)])
			}
		}
	}
}
