import Foundation
import PaneKit

// Transient UI state of the browser window that is not the browser's data:
// the find bar, focus requests, the Ctrl+Tab switcher.
@MainActor
@Observable
final class WindowState {
	struct Find: Equatable {
		var visible = false
		var text = ""
		var matches = 0
		var activeMatch = 0
	}

	struct Switcher: Equatable {
		var visible = false
		var groups: [SwitcherGroup] = []
		var selectedIndex = 0

		var flattened: [SwitcherTab] {
			groups.flatMap(\.tabs)
		}
	}

	// A confirmation shown over the whole window, as the Electron app's
	// Dialog: title, message, and a destructive confirm button.
	struct ConfirmDialog: Identifiable {
		let id = UUID()
		let title: String
		let message: String
		let confirmTitle: String
		let confirm: @MainActor () -> Void
	}

	var find = Find()
	var switcher = Switcher()
	var dialog: ConfirmDialog?
	// The profile sheet: a new profile, or one being edited.
	var profileSheet: ProfileSheetTarget?

	enum ProfileSheetTarget: Equatable {
		case new
		case edit(String)

		var profileID: String? {
			if case let .edit(id) = self {
				return id
			}

			return nil
		}
	}
	// The address field's text: the selected tab's address, or what the user
	// types over it.
	var address = ""
	// A page is in fullscreen: the window shows only the page.
	var pageFullscreen = false
	// "Press Esc to exit full screen", while a page has fullscreen or a lock.
	var exclusiveAccessHint: String?
	// Pages that started printing, for scenarios.
	var printRequests = 0
	// Where each extension's toolbar button is (in the window's SwiftUI
	// coordinates), for its popup to point at.
	var actionAnchors: [String: CGRect] = [:]
	static let extensionsMenuAnchor = "pane.extensions"
	// The extension just installed, for the confirmation the app shows.
	var installedExtension: String?
	// Bumped to move focus into the address field.
	private(set) var addressFocusRequest = 0
	// Bumped to move focus into the find field.
	private(set) var findFocusRequest = 0

	func focusAddressField() {
		addressFocusRequest += 1
	}

	func focusFindField() {
		findFocusRequest += 1
	}
}
