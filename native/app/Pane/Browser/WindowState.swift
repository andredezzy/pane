import AppKit
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

	var find = Find()
	var switcher = Switcher()
	// Quit Pane? (⌘Q asks first.)
	var confirmingQuit = false
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
	// The sidebar's width: its edge's drag changes it at each step, and the
	// settings keep it once the drag ends (SidebarResizeHandle).
	var sidebarWidth = CGFloat(SidebarWidth.standard)
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
	// The extensions menu, open under the toolbar's puzzle.
	var extensionsMenuOpen = false
	// One extension's more menu, beside its row of the extensions menu: the
	// extension and the row's frame (the window's SwiftUI coordinates).
	var extensionMoreMenu: (extensionID: String, row: CGRect)?
	// Adding or removing an extension, asked in a popup under its toolbar
	// button (ExtensionPromptView).
	var extensionPrompt: ExtensionPrompt?
	// Its Report abuse box, ticked.
	var extensionPromptReportsAbuse = false

	struct ExtensionPrompt: Identifiable {
		enum Kind: String {
			case install = "extensionInstall"
			case remove = "extensionRemove"
		}

		let id = UUID()
		let kind: Kind
		let extensionID: String
		let title: String
		let icon: NSImage?
		let permissionsHeading: String
		let permissions: [String]
		let acceptTitle: String
		let cancelTitle: String
		// Answered once: accepted or not, and Report abuse (removal only).
		let answer: @MainActor (_ accepted: Bool, _ reportAbuse: Bool) -> Void
	}

	// Where Settings scrolls as it opens: the Extensions section, or one
	// extension's card in it (their accessibility identifiers).
	var settingsAnchor: String?
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
