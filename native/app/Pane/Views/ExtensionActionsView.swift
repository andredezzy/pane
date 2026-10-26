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
		HStack(spacing: 2) {
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
					Image(systemName: "puzzlepiece.extension")
						.font(.system(size: 12, weight: .medium))
				}
				.menuStyle(.borderlessButton)
				.menuIndicator(.hidden)
				.frame(width: 26, height: 26)
				.foregroundStyle(.secondary)
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

private struct ActionButton: View {
	let item: ExtensionInfo
	let action: () -> Void

	var body: some View {
		Button(action: action) {
			Group {
				if let icon = item.icon {
					Image(nsImage: icon)
						.resizable()
						.interpolation(.high)
				} else {
					Image(systemName: "puzzlepiece.extension")
						.font(.system(size: 12, weight: .medium))
						.foregroundStyle(.secondary)
				}
			}
			.frame(width: 16, height: 16)
			.frame(width: 26, height: 26)
			.overlay(alignment: .bottomTrailing) {
				if !item.badgeText.isEmpty {
					Text(item.badgeText)
						.font(.system(size: 8, weight: .bold))
						.foregroundStyle(Color(nsColor: item.badgeTextColor ?? .white))
						.padding(.horizontal, 2)
						.background(Color(nsColor: item.badgeBackground ?? .systemRed), in: RoundedRectangle(cornerRadius: 3))
						.offset(x: 1, y: 1)
				}
			}
			.contentShape(Rectangle())
		}
		.buttonStyle(.plain)
		.opacity(item.actionEnabled ? 1 : 0.4)
		.hoverBackground(isSelected: false, cornerRadius: 6)
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
