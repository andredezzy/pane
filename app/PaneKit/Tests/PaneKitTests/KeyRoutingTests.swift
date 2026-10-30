import PaneKit
import Testing

private let escape = KeyPress(keyCode: KeyCode.escape, characters: "\u{1b}")
private let returnKey = KeyPress(keyCode: KeyCode.returnKey, characters: "\r")
private let enter = KeyPress(keyCode: KeyCode.keypadEnter, characters: "\u{3}")
private let quit = KeyPress(keyCode: 12, modifiers: .command, characters: "q")
private let ctrlTab = KeyPress(keyCode: KeyCode.tab, modifiers: .control, characters: "\t")
private let controlUp = KeyPress(kind: .modifiersChanged, keyCode: KeyCode.leftControl)

private func letter(_ text: String, _ modifiers: KeyPress.Modifiers = []) -> KeyPress {
	KeyPress(keyCode: 0, modifiers: modifiers, characters: text)
}

private func route(_ key: KeyPress, _ overlays: KeyOverlays) -> KeyRoute {
	KeyRouting.route(key, through: overlays)
}

@Suite("key routing")
struct KeyRoutingTests {
	@Test("with nothing over the window, every key goes on")
	func nothingShows() {
		for key in [escape, returnKey, letter("a"), quit, controlUp] {
			#expect(route(key, KeyOverlays()) == KeyRoute(nil, consumed: false))
		}
	}

	@Test("the PIN screen takes every key but Quit")
	func pinScreen() {
		let overlays = KeyOverlays(pinScreen: true)

		#expect(route(letter("4"), overlays) == KeyRoute(.pinDigit("4"), consumed: true))
		#expect(route(KeyPress(keyCode: KeyCode.delete), overlays) == KeyRoute(.pinBackspace, consumed: true))
		#expect(route(escape, overlays) == KeyRoute(.pinBack, consumed: true))
		#expect(route(enter, overlays) == KeyRoute(.pinContinue, consumed: true))
		// A shortcut reaches neither the menu nor the PIN.
		#expect(route(letter("t", .command), overlays) == KeyRoute(nil, consumed: true))
		#expect(route(ctrlTab, overlays) == KeyRoute(nil, consumed: true))
		#expect(route(quit, overlays) == KeyRoute(nil, consumed: false))
	}

	@Test("⌘Q gets through with Caps Lock on, and only ⌘Q")
	func quitWithCapsLock() {
		let overlays = KeyOverlays(pinScreen: true)
		let capsQuit = KeyPress(keyCode: 12, modifiers: [.command, .capsLock], characters: "Q", charactersIgnoringModifiers: "Q")

		#expect(route(capsQuit, overlays) == KeyRoute(nil, consumed: false))
		#expect(route(letter("q", [.command, .shift]), overlays) == KeyRoute(nil, consumed: true))
	}

	@Test("Quit Pane? goes back on Esc, quits on Return, and takes the rest")
	func quitConfirmation() {
		let overlays = KeyOverlays(quitConfirmation: true)

		#expect(route(escape, overlays) == KeyRoute(.cancelQuit, consumed: true))
		#expect(route(returnKey, overlays) == KeyRoute(.quit, consumed: true))
		#expect(route(letter("a"), overlays) == KeyRoute(nil, consumed: true))
		#expect(route(quit, overlays) == KeyRoute(nil, consumed: false))
	}

	@Test("the App icon picker closes on Esc and Return and lets the rest reach its icons")
	func appIconPicker() {
		let overlays = KeyOverlays(appIconPicker: true)

		#expect(route(escape, overlays) == KeyRoute(.closeAppIconPicker, consumed: true))
		#expect(route(returnKey, overlays) == KeyRoute(.closeAppIconPicker, consumed: true))
		#expect(route(letter("a"), overlays) == KeyRoute(nil, consumed: false))
		#expect(route(escape, KeyOverlays(quitConfirmation: true, appIconPicker: true)).command == .cancelQuit)
	}

	@Test("Keyboard shortcuts closes on Esc and Return; a field recording takes every key")
	func extensionShortcuts() {
		let overlays = KeyOverlays(extensionShortcuts: true)

		#expect(route(escape, overlays) == KeyRoute(.closeExtensionShortcuts, consumed: true))
		#expect(route(returnKey, overlays) == KeyRoute(.closeExtensionShortcuts, consumed: true))
		#expect(route(letter("t", .command), overlays) == KeyRoute(nil, consumed: false))

		var recording = overlays
		recording.recordingShortcut = true

		#expect(route(escape, recording) == KeyRoute(.cancelShortcutRecording, consumed: true))
		#expect(route(ctrlTab, recording) == KeyRoute(.cancelShortcutRecording, consumed: true))
		// Pane's own keys and Quit are typed into the field, not run.
		#expect(route(letter("t", .command), recording) == KeyRoute(.recordShortcut, consumed: true))
		#expect(route(quit, recording) == KeyRoute(.recordShortcut, consumed: true))
		#expect(route(returnKey, recording) == KeyRoute(.recordShortcut, consumed: true))
		#expect(route(KeyPress(kind: .modifiersChanged, keyCode: 55, modifiers: .command), recording) == KeyRoute(.recordShortcut, consumed: true))
		// The PIN screen over it.
		#expect(route(escape, KeyOverlays(pinScreen: true, extensionShortcuts: true, recordingShortcut: true)).command == .pinBack)
	}

	@Test("Add a site cancels on Esc and adds on Return; the rest is typed into its field")
	func addingSite() {
		let overlays = KeyOverlays(addingSite: true)

		#expect(route(escape, overlays) == KeyRoute(.cancelAddSite, consumed: true))
		#expect(route(returnKey, overlays) == KeyRoute(.addSite, consumed: true))
		#expect(route(letter("a"), overlays) == KeyRoute(nil, consumed: false))
		#expect(route(escape, KeyOverlays(quitConfirmation: true, addingSite: true)).command == .cancelQuit)
	}

	@Test("Esc goes to the topmost overlay: Quit Pane? over an extension's popup")
	func quitOverPrompt() {
		let overlays = KeyOverlays(quitConfirmation: true, extensionPrompt: true)

		#expect(route(escape, overlays).command == .cancelQuit)
		#expect(route(returnKey, overlays).command == .quit)
	}

	@Test("the PIN screen over Quit Pane?")
	func pinOverQuit() {
		#expect(route(escape, KeyOverlays(pinScreen: true, quitConfirmation: true)).command == .pinBack)
	}

	@Test("an extension's popup takes all but shortcuts")
	func extensionPrompt() {
		let overlays = KeyOverlays(extensionPrompt: true)

		#expect(route(escape, overlays) == KeyRoute(.cancelExtensionPrompt, consumed: true))
		#expect(route(returnKey, overlays) == KeyRoute(.acceptExtensionPrompt, consumed: true))
		#expect(route(letter("a"), overlays) == KeyRoute(nil, consumed: true))
		#expect(route(letter("t", .command), overlays) == KeyRoute(nil, consumed: false))
		// Ctrl+Tab is a shortcut: the switcher opens.
		#expect(route(ctrlTab, overlays) == KeyRoute(.openSwitcher(backward: false), consumed: true))
	}

	@Test("the popup over the extensions menu, the menu over the switcher")
	func promptOverMenu() {
		#expect(route(escape, KeyOverlays(extensionPrompt: true, extensionsMenu: true)).command == .cancelExtensionPrompt)
		#expect(route(escape, KeyOverlays(extensionsMenu: true, switcher: true)).command == .closeExtensionsMenu)
	}

	@Test("Esc closes the more menu, then the menu; the rest goes on")
	func extensionsMenu() {
		#expect(route(escape, KeyOverlays(extensionsMenu: true, extensionMoreMenu: true)) == KeyRoute(.closeExtensionMoreMenu, consumed: true))
		#expect(route(escape, KeyOverlays(extensionsMenu: true)) == KeyRoute(.closeExtensionsMenu, consumed: true))
		#expect(route(returnKey, KeyOverlays(extensionsMenu: true)) == KeyRoute(nil, consumed: false))
		#expect(route(letter("a"), KeyOverlays(extensionsMenu: true)) == KeyRoute(nil, consumed: false))
	}

	@Test("Ctrl+Tab opens the switcher and steps through it; Control's release picks")
	func switcher() {
		let backward = KeyPress(keyCode: KeyCode.tab, modifiers: [.control, .shift], characters: "\t")

		#expect(route(ctrlTab, KeyOverlays()) == KeyRoute(.openSwitcher(backward: false), consumed: true))
		#expect(route(backward, KeyOverlays()) == KeyRoute(.openSwitcher(backward: true), consumed: true))
		#expect(route(ctrlTab, KeyOverlays(switcher: true)) == KeyRoute(.cycleSwitcher(backward: false), consumed: true))
		#expect(route(escape, KeyOverlays(switcher: true)) == KeyRoute(.cancelSwitcher, consumed: true))
		#expect(route(controlUp, KeyOverlays(switcher: true)) == KeyRoute(.commitSwitcher, consumed: false))
		// Control still held: no pick.
		#expect(route(KeyPress(kind: .modifiersChanged, keyCode: 56, modifiers: [.control, .shift]), KeyOverlays(switcher: true)) == KeyRoute(nil, consumed: false))
		#expect(route(controlUp, KeyOverlays()) == KeyRoute(nil, consumed: false))
		// Tab alone is the page's.
		#expect(route(KeyPress(keyCode: KeyCode.tab, characters: "\t"), KeyOverlays()) == KeyRoute(nil, consumed: false))
	}

	@Test("a page's dialog: Esc dismisses, Return confirms, a field keeps its typing")
	func dialog() {
		let overlays = KeyOverlays(dialog: true)

		#expect(route(escape, overlays) == KeyRoute(.cancelDialog, consumed: true))
		#expect(route(returnKey, overlays) == KeyRoute(.confirmDialog, consumed: true))
		#expect(route(enter, overlays) == KeyRoute(.confirmDialog, consumed: true))
		// Nothing reaches the page under it.
		#expect(route(letter("a"), overlays) == KeyRoute(nil, consumed: true))
		#expect(route(KeyPress(keyCode: KeyCode.tab, characters: "\t"), overlays) == KeyRoute(nil, consumed: true))

		var typing = overlays
		typing.textFieldFocused = true
		#expect(route(letter("a"), typing) == KeyRoute(nil, consumed: false))
		#expect(route(KeyPress(keyCode: KeyCode.tab, characters: "\t"), typing) == KeyRoute(nil, consumed: false))
		#expect(route(escape, typing) == KeyRoute(.cancelDialog, consumed: true))

		#expect(route(letter("w", .command), overlays) == KeyRoute(nil, consumed: false))
		#expect(route(ctrlTab, overlays) == KeyRoute(.openSwitcher(backward: false), consumed: true))
	}

	@Test("the switcher over a dialog, a dialog over a page in fullscreen")
	func dialogOrder() {
		#expect(route(escape, KeyOverlays(switcher: true, dialog: true)).command == .cancelSwitcher)
		#expect(route(escape, KeyOverlays(dialog: true, pageFullscreen: true)).command == .cancelDialog)
		#expect(route(escape, KeyOverlays(extensionPrompt: true, dialog: true)).command == .cancelExtensionPrompt)
	}

	@Test("Esc leaves a page's fullscreen, unless the switcher shows")
	func pageFullscreen() {
		#expect(route(escape, KeyOverlays(pageFullscreen: true)) == KeyRoute(.exitPageFullscreen, consumed: true))
		#expect(route(escape, KeyOverlays(switcher: true, pageFullscreen: true)).command == .cancelSwitcher)
		#expect(route(letter("f"), KeyOverlays(pageFullscreen: true)) == KeyRoute(nil, consumed: false))
	}
}
