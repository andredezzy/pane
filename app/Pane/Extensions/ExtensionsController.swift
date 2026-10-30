import AppKit
import PaneCore
import PaneKit
import SwiftUI

// The extensions feature, beside the browser it runs in: each open
// profile's extensions as the engine last reported them, the toolbar
// puzzle's menu (ExtensionsMenu) and an extension's more menu beside it, the
// Extensions page's Details of one and its Keyboard shortcuts dialog, the
// popups that add or remove one (ExtensionPromptView), and the action popups
// (ExtensionPopover). Esc closes the topmost, Return accepts a popup
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
	// Where the Extensions page's list scrolls as it shows: one extension's
	// card (its accessibility identifier).
	var pageAnchor: String?
	// The extension the Extensions page shows the Details of, instead of the
	// list.
	private(set) var details: ShownDetails?
	// The Extensions page's Keyboard shortcuts dialog (ADR 0104).
	private(set) var shortcuts: ShortcutsDialog?
	// Details' Add a site dialog (Paper: 09.16).
	private(set) var addingSite: AddSiteDialog?
	// Shows extension action popups.
	let popover = ExtensionPopover()

	@ObservationIgnored private unowned let browser: BrowserController
	// The context whose shortcuts a recording paused, to resume.
	@ObservationIgnored private var pausedContext: BrowserContext?

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

		if details?.profileID == profileID {
			refreshDetails()
		}

		if shortcuts?.profileID == profileID {
			shortcuts?.extensions = shortcutCommands(profileID)
		}
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

	// The active profile's extension shortcut (chrome.commands) `event` is,
	// run; false when it is none. Extensions take only chords (⌘, ⌃, or ⌥).
	func runShortcut(_ event: NSEvent) -> Bool {
		guard event.type == .keyDown, !event.modifierFlags.isDisjoint(with: [.command, .control, .option]),
		      let profileID = model.activeProfileID, let context = browser.context(for: profileID) else {
			return false
		}

		return context.runExtensionShortcut(event, in: browser.browserWindow)
	}

	// The extension's own items and its site access for its action's
	// right-click menu, as for the active tab; nil when it has neither.
	func actionMenu(_ extensionID: String) -> ExtensionActionMenu? {
		guard let profileID = model.activeProfileID, let context = browser.context(for: profileID) else {
			return nil
		}

		return context.actionMenu(ofExtension: extensionID, in: browser.browserWindow)
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

	// Details' Site access pop-up. The engine changes it at once, so Details
	// shows it in the same frame, before the engine reports the change. On
	// specific sites, chosen from another, asks for a site first, as
	// Chrome's details page does: it holds none yet.
	func setSiteAccess(_ extensionID: String, _ access: ExtensionSiteAccess, inProfile profileID: String) {
		guard let context = browser.context(for: profileID) else {
			return
		}

		if access == .onSpecificSites, context.details(ofExtension: extensionID)?.siteAccess != .onSpecificSites {
			showAddSite(extensionID, inProfile: profileID)
			return
		}

		context.setSiteAccess(access, forExtension: extensionID)
		refreshDetails()
	}

	// Details' Add a site, under the list or from On specific sites.
	func showAddSite(_ extensionID: String, inProfile profileID: String) {
		addingSite = AddSiteDialog(profileID: profileID, extensionID: extensionID)
	}

	// What the dialog's field holds; an edit clears its message. (The field
	// also writes back what it holds, which is no edit.)
	func editSite(_ site: String) {
		guard addingSite != nil, site != addingSite?.site else {
			return
		}

		addingSite?.site = site
		addingSite?.invalid = false
	}

	// Add, or Return: the site joins the list, with the extension On
	// specific sites; a site Chrome refuses keeps the dialog, with its
	// message.
	func addSite() {
		guard let dialog = addingSite, !dialog.site.isEmpty, let context = browser.context(for: dialog.profileID) else {
			return
		}

		guard context.addSite(dialog.site, toExtension: dialog.extensionID) else {
			addingSite?.invalid = true
			return
		}

		addingSite = nil
		refreshDetails()
	}

	func cancelAddSite() {
		addingSite = nil
	}

	// A site's ×; with none left, the extension runs on click.
	func removeSite(_ site: String, of extensionID: String, inProfile profileID: String) {
		browser.context(for: profileID)?.removeSite(site, fromExtension: extensionID)
		refreshDetails()
	}

	// Details' Allow access to file URLs; the engine reloads the extension.
	func setFileAccess(_ extensionID: String, _ allowed: Bool, inProfile profileID: String) {
		browser.context(for: profileID)?.setAllowsFileAccess(allowed, forExtension: extensionID)
		refreshDetails()
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

	// The Extensions page, its list.
	func showPage() {
		closeMenu()
		details = nil
		browser.showPage(.extensions)
	}

	// The Extensions page at an extension's Details, in a profile (the active
	// one by default): a card's Details, and Manage Extension and View Web
	// Permissions, which open chrome://extensions/?id= in Chrome. Its size is
	// measured off the main thread and joins it once known.
	func showDetails(_ extensionID: String, inProfile profileID: String? = nil) {
		closeMenu()

		guard let profileID = profileID ?? model.activeProfileID else {
			return
		}

		details = ShownDetails(profileID: profileID, extensionID: extensionID)
		refreshDetails()
		browser.showPage(.extensions)

		browser.context(for: profileID)?.size(ofExtension: extensionID) { [weak self] size in
			guard let self, details?.extensionID == extensionID, details?.profileID == profileID else {
				return
			}

			details?.size = size
		}
	}

	// Details' back arrow: the list, at the extension's card.
	func closeDetails() {
		pageAnchor = details.map { "pane.extensions.extension.\($0.extensionID)" }
		details = nil
	}

	// What the engine holds of the shown extension now; the list once it is
	// removed.
	private func refreshDetails() {
		guard let shown = details, let context = browser.context(for: shown.profileID) else {
			return
		}

		guard let engine = context.details(ofExtension: shown.extensionID) else {
			details = nil
			addingSite = nil
			return
		}

		details?.engine = engine
	}

	// MARK: Keyboard shortcuts

	// What the dialog lists, as chrome://extensions/shortcuts does: the
	// profile's enabled extensions that have commands, each with them.
	func shortcutCommands(_ profileID: String) -> [ExtensionCommands] {
		guard let context = browser.context(for: profileID) else {
			return []
		}

		return list(profileID).filter(\.enabled).compactMap { item in
			let commands = context.commands(ofExtension: item.id)

			return commands.isEmpty ? nil : ExtensionCommands(item: item, commands: commands)
		}
	}

	// The header's Keyboard shortcuts, for the profile the page lists.
	func showShortcuts(inProfile profileID: String) {
		withAnimation(Motion.panel) {
			shortcuts = ShortcutsDialog(profileID: profileID, extensions: shortcutCommands(profileID))
		}
	}

	// Done, Return, Esc, or a click outside it.
	func closeShortcuts() {
		cancelRecording()

		withAnimation(Motion.panel) {
			shortcuts = nil
		}
	}

	// A click on a field: it records the next key, and no extension's
	// shortcut runs meanwhile, as Chrome suspends them.
	func record(_ field: CommandField) {
		guard shortcuts != nil else {
			return
		}

		if shortcuts?.recording == nil, let profileID = model.activeProfileID, let context = browser.context(for: profileID) {
			context.setExtensionShortcutsSuspended(true, in: browser.browserWindow)
			pausedContext = context
		}

		shortcuts?.recording = field
		shortcuts?.held = []
		shortcuts?.refusal = nil
		shortcuts?.moved = nil
	}

	// Esc, Tab, or a click outside the field: it keeps its shortcut.
	func cancelRecording() {
		guard shortcuts?.recording != nil else {
			return
		}

		shortcuts?.recording = nil
		shortcuts?.held = []
		pausedContext?.setExtensionShortcutsSuspended(false, in: browser.browserWindow)
		pausedContext = nil

		// A message goes as the dialog shrinks around it.
		withAnimation(Motion.panel) {
			shortcuts?.refusal = nil
		}
	}

	// A key, or the modifiers held, while a field records (KeyRouter). The
	// modifiers show as they go down. A key Chrome's rules refuse, or one the
	// menu bar holds (ADR 0098), leaves a message under the field, which
	// keeps recording; another sets the shortcut, taking it from a command
	// that had it, as Chrome does.
	func recordKey(_ event: NSEvent) {
		guard let dialog = shortcuts, let field = dialog.recording else {
			return
		}

		let key = PaneKit.KeyPress(event)

		guard key.kind == .down else {
			shortcuts?.held = key.modifiers
			return
		}

		// Its modifiers are spent: the field reads Type a shortcut again.
		shortcuts?.held = []

		let shortcut = BrowserContext.extensionShortcut(for: event)

		// The message comes as the dialog grows around it.
		if let refusal = Self.refusal(of: event, key.modifiers, shortcut: shortcut) {
			withAnimation(Motion.panel) {
				shortcuts?.refusal = refusal
			}

			return
		}

		guard let shortcut else {
			return
		}

		// A command of the same extension that had the key, named by its
		// description; another extension, by its name.
		let sibling = dialog.extensions.first { $0.id == field.extensionID }?.commands.first { $0.shortcut == shortcut }
		let movedFrom = setShortcut(shortcut, of: field, inProfile: dialog.profileID)
		cancelRecording()

		if let movedFrom {
			let name = movedFrom == field.extensionID ? sibling?.summary : list(dialog.profileID).first { $0.id == movedFrom }?.name

			withAnimation(Motion.panel) {
				shortcuts?.moved = (field, name ?? movedFrom)
			}
		}
	}

	// Chrome's rules, on the modifiers then the key, then the menu bar's keys,
	// which stay Pane's (ADR 0098).
	private static func refusal(of event: NSEvent, _ modifiers: PaneKit.KeyPress.Modifiers, shortcut: String?) -> ExtensionShortcut.Refusal? {
		if let refusal = ExtensionShortcut.refusal(modifiers) {
			return refusal
		}

		guard let shortcut else {
			return .typeALetter
		}

		return MainMenu.itemTitle(for: event).map { .panes(item: $0, shortcut: shortcut) }
	}

	// A field's Clear: Not set, as Chrome's empties it.
	func clearShortcut(_ field: CommandField) {
		guard let profileID = shortcuts?.profileID else {
			return
		}

		cancelRecording()
		shortcuts?.moved = nil
		setShortcut("", of: field, inProfile: profileID)
	}

	// Sets a command's shortcut ("" clears it), the dialog showing it at once;
	// the extension whose command had the key, if any.
	@discardableResult
	func setShortcut(_ shortcut: String, of field: CommandField, inProfile profileID: String) -> String? {
		let movedFrom = browser.context(for: profileID)?.setShortcut(shortcut, ofCommand: field.command, extension: field.extensionID)

		if shortcuts?.profileID == profileID {
			shortcuts?.extensions = shortcutCommands(profileID)
		}

		return movedFrom
	}

	// The Chrome Web Store in a new tab of the profile (the active one by
	// default): its Add to Pane (Add to Chrome, renamed by the engine) installs into that profile.
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

	// Details' website and Web Store page.
	func openLink(_ url: URL, inProfile profileID: String) {
		browser.openTab(inProfile: profileID, url: url)
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

// The extension Details shows, in its profile: what the engine adds to its
// snapshot (ExtensionInfo), and the size of its files once measured.
struct ShownDetails {
	let profileID: String
	let extensionID: String
	var engine: ExtensionDetails?
	var size: String?
}

// Details' Add a site: the extension, what the field holds, and whether
// Chrome refused it.
struct AddSiteDialog {
	let profileID: String
	let extensionID: String
	var site = ""
	var invalid = false
}

// The Keyboard shortcuts dialog (ADR 0104): a profile's extensions with
// their commands, the field recording a key, with the modifiers held and
// why the last key was refused, and the field a key last moved to, with
// where it came from.
struct ShortcutsDialog {
	let profileID: String
	var extensions: [ExtensionCommands]
	var recording: CommandField?
	var held = PaneKit.KeyPress.Modifiers()
	var refusal: ExtensionShortcut.Refusal?
	var moved: (field: CommandField, from: String)?
}

// An extension and its commands, as the dialog lists them.
struct ExtensionCommands: Identifiable {
	let item: ExtensionInfo
	let commands: [ExtensionCommand]

	var id: String {
		item.id
	}
}

// One command's shortcut field.
struct CommandField: Hashable {
	let extensionID: String
	let command: String
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
