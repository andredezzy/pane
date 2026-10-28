import AppKit
import PaneCore
import PaneKit

// The extensions feature, beside the browser it runs in: each open
// profile's extensions as the engine last reported them, the toolbar
// puzzle's menu (ExtensionsMenu) and an extension's more menu beside it, the
// popups that add or remove one (ExtensionPromptView), and the action
// popups (ExtensionPopover). Esc closes the topmost, Return accepts a popup
// (KeyRouter); a menu also closes on a click outside it, or once one of its
// items runs.
@MainActor
@Observable
final class ExtensionsController {
	static let webStoreURL = URL(string: "https://chromewebstore.google.com/category/extensions")!
	// The puzzle's key among the toolbar's anchors.
	static let menuAnchor = "pane.extensions"

	// Each open profile's extensions.
	private(set) var lists: [String: [ExtensionInfo]] = [:]
	// The extensions menu, open under the toolbar's puzzle.
	private(set) var menuOpen = false
	// One extension's more menu, beside its row of the extensions menu: the
	// extension and the row's frame (the window's SwiftUI coordinates).
	private(set) var moreMenu: (extensionID: String, row: CGRect)?
	// Adding or removing an extension, asked in a popup under its toolbar
	// button.
	private(set) var prompt: ExtensionPrompt?
	// Its Report abuse box, ticked.
	var promptReportsAbuse = false
	// Where each extension's toolbar button is (in the window's SwiftUI
	// coordinates), and the puzzle (menuAnchor), for popups to point at.
	var actionAnchors: [String: CGRect] = [:]
	// Where the Extensions page scrolls as it opens: one extension's card
	// (its accessibility identifier).
	var pageAnchor: String?
	// Shows extension action popups.
	let popover = ExtensionPopover()

	@ObservationIgnored private unowned let browser: BrowserController

	init(browser: BrowserController) {
		self.browser = browser
	}

	private var model: BrowserModel {
		browser.model
	}

	// The active profile's extensions.
	var active: [ExtensionInfo] {
		model.activeProfileID.flatMap { lists[$0] } ?? []
	}

	func list(_ profileID: String) -> [ExtensionInfo] {
		lists[profileID] ?? []
	}

	// MARK: The engine's extensions

	// What the profile's open context holds now.
	func refresh(_ profileID: String) {
		guard let context = browser.context(for: profileID) else {
			return
		}

		lists[profileID] = context.extensions.map(ExtensionInfo.init)
	}

	// A deleted profile's go with it.
	func forget(_ profileID: String) {
		lists[profileID] = nil
	}

	// Opens the profile's engine context if it is not open yet, so its
	// extensions can be listed and managed.
	func openContextIfNeeded(profileID: String) {
		browser.withContext(profileID) { [weak self] _ in
			self?.refresh(profileID)
		}
	}

	// The extension's toolbar action in the active profile: its popup, or
	// chrome.action.onClicked.
	func runAction(_ extensionID: String) {
		guard let profileID = model.activeProfileID, let context = browser.context(for: profileID) else {
			return
		}

		context.runAction(ofExtension: extensionID, in: browser.browserWindow)
	}

	// Installs from the Web Store into a profile; the engine asks first.
	func install(_ identifier: String, inProfile profileID: String) {
		browser.withContext(profileID) { [weak self] context in
			guard let self, let context else {
				return
			}

			context.installExtension(identifier: identifier, window: browser.browserWindow)
		}
	}

	func setEnabled(_ extensionID: String, _ enabled: Bool, inProfile profileID: String) {
		browser.context(for: profileID)?.setEnabled(enabled, forExtension: extensionID)
	}

	func setPinned(_ extensionID: String, _ pinned: Bool, inProfile profileID: String) {
		browser.context(for: profileID)?.setPinned(pinned, forExtension: extensionID)
	}

	func uninstall(_ extensionID: String, inProfile profileID: String) {
		browser.context(for: profileID)?.uninstallExtension(extensionID)
	}

	func update(inProfile profileID: String) {
		browser.context(for: profileID)?.updateExtensions()
	}

	// The engine asks before adding: in the window, as Aside's popup, since
	// an AppKit sheet takes some 280 ms to attach, with the main thread held.
	func confirmInstall(_ request: ExtensionInstallRequest) {
		present(ExtensionPrompt(
			kind: .install,
			extensionID: request.extensionIdentifier,
			title: ExtensionPromptText.title(acceptTitle: request.acceptTitle, extensionName: request.extensionName, chromeTitle: request.title),
			icon: request.icon,
			permissionsHeading: request.permissionsHeading,
			permissions: request.permissions,
			acceptTitle: ExtensionPromptText.acceptTitle(request.acceptTitle),
			cancelTitle: request.cancelTitle,
			answer: { accepted, _ in accepted ? request.accept() : request.cancel() }
		))
	}

	// Removes after asking, in the popup, as Aside does; Report abuse then
	// opens the Web Store's report page, as Chrome's dialog does.
	func confirmUninstall(_ item: ExtensionInfo, inProfile profileID: String) {
		present(ExtensionPrompt(
			kind: .remove,
			extensionID: item.id,
			title: ExtensionPromptText.removeTitle(item.name),
			icon: item.icon,
			permissionsHeading: "",
			permissions: [],
			acceptTitle: "Remove",
			cancelTitle: "Cancel",
			answer: { [weak self] accepted, reportAbuse in
				guard accepted, let self else {
					return
				}

				uninstall(item.id, inProfile: profileID)

				if reportAbuse, let url = ExtensionPromptText.reportAbuseURL(item.id) {
					browser.openTab(inProfile: profileID, url: url)
				}
			}
		))
	}

	// An action's popup page, under its toolbar button, else the puzzle.
	// Behind the lock it closes at once.
	func show(_ popup: ExtensionPopup) {
		guard browser.interactionAllowed, let hostingView = browser.hostingView else {
			popup.close()
			return
		}

		let anchor = actionAnchors[popup.extensionIdentifier]
			?? actionAnchors[Self.menuAnchor]
			?? NSRect(x: hostingView.bounds.maxX - 48, y: 8, width: 26, height: 26)
		popover.show(popup, relativeTo: anchor, of: hostingView)
	}

	// MARK: The menu

	func toggleMenu() {
		if menuOpen {
			closeMenu()
			return
		}

		guard model.activeProfileID != nil else {
			return
		}

		menuOpen = true
	}

	// The menu, and the more menu beside it.
	func closeMenu() {
		moreMenu = nil
		menuOpen = false
	}

	func toggleMoreMenu(_ extensionID: String, row: CGRect) {
		moreMenu = moreMenu?.extensionID == extensionID ? nil : (extensionID, row)
	}

	func closeMoreMenu() {
		moreMenu = nil
	}

	// A row's pin, the more menu's Pin and Unpin, a pinned button's Unpin:
	// in the active profile, the more menu closed.
	func togglePinned(_ item: ExtensionInfo) {
		guard let profileID = model.activeProfileID else {
			return
		}

		moreMenu = nil
		setPinned(item.id, !item.pinned, inProfile: profileID)
	}

	// A row of the menu: the extension's action, its popup at the puzzle when
	// it is not pinned.
	func runFromMenu(_ extensionID: String) {
		closeMenu()
		runAction(extensionID)
	}

	// The Extensions page, at one extension's card if given.
	func showPage(extensionID: String? = nil) {
		closeMenu()
		pageAnchor = extensionID.map { "pane.extensions.extension.\($0)" }
		browser.showPage(.extensions)
	}

	// The Chrome Web Store in a new tab of the profile (the active one by
	// default): its Add to Chrome installs into that profile.
	func visitWebStore(inProfile profileID: String? = nil) {
		closeMenu()

		if let profileID = profileID ?? model.activeProfileID {
			browser.openTab(inProfile: profileID, url: Self.webStoreURL)
		}
	}

	// An extension's page in the Web Store, where it came from.
	func openStorePage(_ extensionID: String) {
		closeMenu()

		if let profileID = model.activeProfileID, let url = URL(string: "https://chromewebstore.google.com/detail/\(extensionID)") {
			browser.openTab(inProfile: profileID, url: url)
		}
	}

	func openOptions(_ item: ExtensionInfo, inProfile profileID: String? = nil) {
		closeMenu()

		if let profileID = profileID ?? model.activeProfileID, let url = item.optionsURL {
			browser.openTab(inProfile: profileID, url: url)
		}
	}

	// Remove from Pane, in the active profile.
	func confirmUninstall(_ item: ExtensionInfo) {
		if let profileID = model.activeProfileID {
			confirmUninstall(item, inProfile: profileID)
		}
	}

	// MARK: The popups

	// A popup replaces one still open, which answers no. Behind the lock the
	// answer is no at once.
	func present(_ prompt: ExtensionPrompt) {
		guard browser.interactionAllowed else {
			prompt.answer(false, false)
			return
		}

		closeMenu()
		let previous = self.prompt
		promptReportsAbuse = false
		self.prompt = prompt
		previous?.answer(false, false)
	}

	func answerPrompt(accepted: Bool) {
		guard let prompt else {
			return
		}

		self.prompt = nil
		prompt.answer(accepted, accepted && promptReportsAbuse)
	}
}

// Adding or removing an extension, as its popup asks.
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
