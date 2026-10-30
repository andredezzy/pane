import AppKit
import PaneKit

// Pane's one key monitor. A local event monitor sees every key before any
// view (a page included) and the menu bar's shortcuts. It asks the overlays
// that show, topmost first, as they are drawn, whether one takes the key
// (PaneKit KeyRouting: the PIN screen, Quit Pane?, the App icon picker, the
// Keyboard shortcuts dialog, an extension's popup, the extensions menu, the
// tab switcher, a page's dialog, a page's fullscreen), then runs what it
// answered, then the extensions' shortcuts.
@MainActor
final class KeyRouter {
	private unowned let controller: BrowserController
	private var monitor: Any?

	init(controller: BrowserController) {
		self.controller = controller
		monitor = NSEvent.addLocalMonitorForEvents(matching: [.keyDown, .flagsChanged]) { [weak self] event in
			// Event monitors run on the main thread.
			nonisolated(unsafe) let event = event
			let consumed = MainActor.assumeIsolated { self?.handle(event) ?? false }

			return consumed ? nil : event
		}
	}

	// Whether `event` stops here and must not reach the window. A key the
	// overlays leave may be an extension's shortcut (chrome.commands), which
	// goes before the page, as in Chrome.
	private func handle(_ event: NSEvent) -> Bool {
		let route = KeyRouting.route(KeyPress(event), through: overlays)

		if let command = route.command {
			perform(command, event)
		}

		return route.consumed || controller.extensions.runShortcut(event)
	}

	private var overlays: KeyOverlays {
		let state = controller.state
		let extensions = controller.extensions

		return KeyOverlays(
			pinScreen: controller.lock.entry != nil,
			quitConfirmation: state.confirmingQuit,
			appIconPicker: state.choosingAppIcon,
			extensionShortcuts: extensions.shortcuts != nil,
			recordingShortcut: extensions.shortcuts?.recording != nil,
			extensionPrompt: extensions.prompt != nil,
			extensionsMenu: extensions.menuOpen,
			extensionMoreMenu: extensions.moreMenu != nil,
			switcher: state.switcher.visible,
			dialog: controller.dialogs.dialog != nil,
			pageFullscreen: state.pageFullscreen,
			// A field being edited: its field editor has the keyboard.
			textFieldFocused: controller.browserWindow.window.firstResponder is NSTextView
		)
	}

	private func perform(_ command: KeyCommand, _ event: NSEvent) {
		let lock = controller.lock
		let extensions = controller.extensions

		switch command {
		case let .pinDigit(digit): lock.digit(digit)
		case .pinBackspace: lock.backspace()
		case .pinBack: lock.dismiss()
		case .pinContinue: lock.continueWithPin()
		case .cancelQuit: controller.cancelQuit()
		case .quit: controller.quit()
		case .closeAppIconPicker: controller.closeAppIconPicker()
		case .closeExtensionShortcuts: extensions.closeShortcuts()
		case .recordShortcut: extensions.recordKey(event)
		case .cancelShortcutRecording: extensions.cancelRecording()
		case .cancelExtensionPrompt: extensions.answerPrompt(accepted: false)
		case .acceptExtensionPrompt: extensions.answerPrompt(accepted: true)
		case .closeExtensionMoreMenu: extensions.closeMoreMenu()
		case .closeExtensionsMenu: extensions.closeMenu()
		case let .openSwitcher(backward): controller.openSwitcher(backward: backward)
		case let .cycleSwitcher(backward): controller.cycleSwitcher(backward: backward)
		case .cancelSwitcher: controller.cancelSwitcher()
		case .commitSwitcher: controller.commitSwitcher()
		case .cancelDialog: controller.dialogs.dismiss()
		case .confirmDialog: controller.dialogs.confirm()
		case .exitPageFullscreen: controller.activePage?.exitFullscreen()
		}
	}
}

extension KeyPress {
	init(_ event: NSEvent) {
		let flags = event.modifierFlags.intersection(.deviceIndependentFlagsMask)
		var modifiers = Modifiers()

		for (flag, modifier) in [(NSEvent.ModifierFlags.command, Modifiers.command), (.control, .control), (.option, .option), (.shift, .shift), (.capsLock, .capsLock)] where flags.contains(flag) {
			modifiers.insert(modifier)
		}

		let isKey = event.type == .keyDown

		self.init(
			kind: isKey ? .down : .modifiersChanged,
			keyCode: event.keyCode,
			modifiers: modifiers,
			characters: isKey ? event.characters : nil,
			charactersIgnoringModifiers: isKey ? event.charactersIgnoringModifiers : nil
		)
	}
}
