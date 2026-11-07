import AppKit
import PaneKit

// Transient UI state of the browser window that is not the browser's data:
// the find bar, the address field's text, the Ctrl+Tab switcher.
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
	// Settings' App icon picker (ADR 0089).
	var choosingAppIcon = false
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
	// The address last put in the field, to tell the user's typing from it, and
	// whose it is.
	var shownAddress = ""
	var addressTabID: String?
	// The field, focused, has had all of its text selected.
	var addressIsSelected = false
	// The sidebar's width: its edge's drag changes it at each step, and the
	// settings keep it once the drag ends (SidebarResizeHandle).
	var sidebarWidth = CGFloat(SidebarWidth.standard)
	// A page is in fullscreen: the window shows only the page.
	var pageFullscreen = false
	// "Press Esc to exit full screen", while a page has fullscreen or a lock.
	var exclusiveAccessHint: String?
}
