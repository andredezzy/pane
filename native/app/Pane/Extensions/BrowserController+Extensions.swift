import AppKit
import PaneCore
import PaneKit

// MARK: - Extensions

extension BrowserController {
	// Opens the profile's engine context if it is not open yet, so its
	// extensions can be listed and managed.
	func openContextIfNeeded(profileID: String) {
		withContext(profileID) { [weak self] _ in
			self?.refreshExtensions(profileID)
		}
	}

	func refreshExtensions(_ profileID: String) {
		guard let context = context(for: profileID) else {
			return
		}

		model.setExtensions(profileID, context.extensions.map(ExtensionInfo.init))
	}

	// The extension's toolbar action in the active profile: its popup, or
	// chrome.action.onClicked.
	func runExtensionAction(_ extensionID: String) {
		guard let profileID = model.activeProfileID, let context = context(for: profileID) else {
			return
		}

		context.runAction(ofExtension: extensionID, in: browserWindow)
	}

	// Installs from the Web Store into a profile; the engine asks first.
	func installExtension(_ identifier: String, inProfile profileID: String) {
		withContext(profileID) { [weak self] context in
			guard let self, let context else {
				return
			}

			context.installExtension(identifier: identifier, window: browserWindow)
		}
	}

	func setExtension(_ extensionID: String, enabled: Bool, inProfile profileID: String) {
		context(for: profileID)?.setEnabled(enabled, forExtension: extensionID)
	}

	func setExtension(_ extensionID: String, pinned: Bool, inProfile profileID: String) {
		context(for: profileID)?.setPinned(pinned, forExtension: extensionID)
	}

	func uninstallExtension(_ extensionID: String, inProfile profileID: String) {
		context(for: profileID)?.uninstallExtension(extensionID)
	}

	func updateExtensions(inProfile profileID: String) {
		context(for: profileID)?.updateExtensions()
	}

	// Adding names Pane, as Dia's dialog names Dia; Chrome's other prompts
	// (new permissions, re-enabling) keep their titles.
	static func installTitle(_ request: ExtensionInstallRequest) -> String {
		request.acceptTitle == "Add extension"
			? "Add \u{201C}\(request.extensionName)\u{201D} to Pane?"
			: curlyQuoted(request.title)
	}

	// Chrome's prompt titles quote the name straight (Add "Dark Reader"?);
	// the popups quote it as Remove does.
	static func curlyQuoted(_ title: String) -> String {
		guard let open = title.firstIndex(of: "\""), let close = title.lastIndex(of: "\""), open < close else {
			return title
		}

		return title.replacingCharacters(in: close ... close, with: "\u{201D}").replacingCharacters(in: open ... open, with: "\u{201C}")
	}

	// Removes after asking, in the popup, as Aside does; Report abuse then
	// opens the Web Store's report page, as Chrome's dialog does.
	func confirmUninstall(_ item: ExtensionInfo, inProfile profileID: String) {
		presentExtensionPrompt(WindowState.ExtensionPrompt(
			kind: .remove,
			extensionID: item.id,
			title: "Remove \u{201C}\(item.name)\u{201D}?",
			icon: item.icon,
			permissionsHeading: "",
			permissions: [],
			acceptTitle: "Remove",
			cancelTitle: "Cancel",
			answer: { [weak self] accepted, reportAbuse in
				guard accepted, let self else {
					return
				}

				uninstallExtension(item.id, inProfile: profileID)

				if reportAbuse, let url = URL(string: "https://chrome.google.com/webstore/report/\(item.id)?utm_source=chrome-remove-extension-dialog") {
					openTab(inProfile: profileID, url: url)
				}
			}
		))
	}
}

// MARK: - Extensions menu and popups

// The toolbar puzzle's menu (ExtensionsMenu), an extension's more menu
// beside it, and the popups that add or remove one (ExtensionPromptView), as
// Aside's. Esc closes the topmost; a menu also closes on a click outside it,
// or once one of its items runs.
extension BrowserController {
	static let webStoreURL = URL(string: "https://chromewebstore.google.com/category/extensions")!

	func toggleExtensionsMenu() {
		if state.extensionsMenuOpen {
			closeExtensionsMenu()
			return
		}

		guard model.activeProfileID != nil else {
			return
		}

		state.extensionsMenuOpen = true
		syncExtensionsKeyMonitor()
	}

	// The menu, and the more menu beside it.
	func closeExtensionsMenu() {
		state.extensionMoreMenu = nil
		state.extensionsMenuOpen = false
		syncExtensionsKeyMonitor()
	}

	func toggleExtensionMoreMenu(_ extensionID: String, row: CGRect) {
		state.extensionMoreMenu = state.extensionMoreMenu?.extensionID == extensionID ? nil : (extensionID, row)
	}

	// The Extensions page, at one extension's card if given.
	func showExtensions(extensionID: String? = nil) {
		closeExtensionsMenu()
		state.extensionsAnchor = extensionID.map { "pane.extensions.extension.\($0)" }
		model.page = .extensions
	}

	// The Chrome Web Store in a new tab of the profile (the active one by
	// default): its Add to Chrome installs into that profile.
	func visitWebStore(inProfile profileID: String? = nil) {
		closeExtensionsMenu()

		if let profileID = profileID ?? model.activeProfileID {
			openTab(inProfile: profileID, url: Self.webStoreURL)
		}
	}

	// An extension's page in the Web Store, where it came from.
	func openExtensionStorePage(_ extensionID: String) {
		closeExtensionsMenu()

		if let profileID = model.activeProfileID, let url = URL(string: "https://chromewebstore.google.com/detail/\(extensionID)") {
			openTab(inProfile: profileID, url: url)
		}
	}

	func openExtensionOptions(_ item: ExtensionInfo, inProfile profileID: String? = nil) {
		closeExtensionsMenu()

		if let profileID = profileID ?? model.activeProfileID, let url = item.optionsURL {
			openTab(inProfile: profileID, url: url)
		}
	}

	// A row of the menu: the extension's action, its popup at the puzzle when
	// it is not pinned.
	func runExtensionFromMenu(_ extensionID: String) {
		closeExtensionsMenu()
		runExtensionAction(extensionID)
	}

	// A popup replaces one still open, which answers no. Behind the lock the
	// answer is no at once.
	func presentExtensionPrompt(_ prompt: WindowState.ExtensionPrompt) {
		guard interactionAllowed else {
			prompt.answer(false, false)
			return
		}

		closeExtensionsMenu()
		let previous = state.extensionPrompt
		state.extensionPromptReportsAbuse = false
		state.extensionPrompt = prompt
		syncExtensionsKeyMonitor()
		previous?.answer(false, false)
	}

	func answerExtensionPrompt(accepted: Bool) {
		guard let prompt = state.extensionPrompt else {
			return
		}

		state.extensionPrompt = nil
		syncExtensionsKeyMonitor()
		prompt.answer(accepted, accepted && state.extensionPromptReportsAbuse)
	}

	// The keys belong to whichever of them shows: a popup takes all but
	// shortcuts, as a dialog would.
	private func syncExtensionsKeyMonitor() {
		let showing = state.extensionsMenuOpen || state.extensionPrompt != nil

		if showing, extensionsKeyMonitor == nil {
			extensionsKeyMonitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { [weak self] event in
				// Event monitors run on the main thread.
				nonisolated(unsafe) let event = event
				let consumed = MainActor.assumeIsolated { self?.extensionsUIConsumes(event) ?? false }

				return consumed ? nil : event
			}
		} else if !showing, let extensionsKeyMonitor {
			NSEvent.removeMonitor(extensionsKeyMonitor)
			self.extensionsKeyMonitor = nil
		}
	}

	private func extensionsUIConsumes(_ event: NSEvent) -> Bool {
		let isPrompt = state.extensionPrompt != nil

		switch event.keyCode {
		case 53:
			if isPrompt {
				answerExtensionPrompt(accepted: false)
			} else if state.extensionMoreMenu != nil {
				state.extensionMoreMenu = nil
			} else {
				closeExtensionsMenu()
			}

			return true

		case 36, 76:
			guard isPrompt else {
				return false
			}

			answerExtensionPrompt(accepted: true)
			return true

		default:
			return isPrompt && event.modifierFlags.intersection(.deviceIndependentFlagsMask).isDisjoint(with: [.command, .control])
		}
	}
}
