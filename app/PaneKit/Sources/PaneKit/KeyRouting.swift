// Which of Pane's overlays a key goes to. The app has one key monitor
// (KeyRouter), which sees every key before the window, the menu bar's
// shortcuts, and any page; it asks this, with what shows, and runs the
// command. The overlays are asked topmost first, as they are drawn: the PIN
// screen, Quit Pane?, the App icon picker, an extension's popup, the extensions menu, the tab
// switcher, a page's dialog, then a page in fullscreen. The first that
// takes the key ends it.

// The keys Pane's overlays take, by key code (Carbon's kVK_ values).
public enum KeyCode {
	public static let returnKey: UInt16 = 36
	public static let tab: UInt16 = 48
	public static let delete: UInt16 = 51
	public static let escape: UInt16 = 53
	public static let leftControl: UInt16 = 59
	public static let keypadEnter: UInt16 = 76
}

// A key as the router reads it: AppKit's key down or modifier change.
public struct KeyPress: Equatable, Sendable {
	public enum Kind: Sendable {
		case down
		case modifiersChanged
	}

	public struct Modifiers: OptionSet, Sendable {
		public let rawValue: Int

		public init(rawValue: Int) {
			self.rawValue = rawValue
		}

		public static let command = Modifiers(rawValue: 1 << 0)
		public static let control = Modifiers(rawValue: 1 << 1)
		public static let option = Modifiers(rawValue: 1 << 2)
		public static let shift = Modifiers(rawValue: 1 << 3)
		public static let capsLock = Modifiers(rawValue: 1 << 4)
	}

	public var kind: Kind
	public var keyCode: UInt16
	public var modifiers: Modifiers
	// What the key types, and what it types without its modifiers.
	public var characters: String?
	public var charactersIgnoringModifiers: String?

	public init(kind: Kind = .down, keyCode: UInt16, modifiers: Modifiers = [], characters: String? = nil, charactersIgnoringModifiers: String? = nil) {
		self.kind = kind
		self.keyCode = keyCode
		self.modifiers = modifiers
		self.characters = characters
		self.charactersIgnoringModifiers = charactersIgnoringModifiers ?? characters
	}

	// ⌘Q, Caps Lock or not: the key that always reaches Quit.
	public var isQuit: Bool {
		kind == .down && modifiers.subtracting(.capsLock) == .command && charactersIgnoringModifiers?.lowercased() == "q"
	}

	var isEscape: Bool {
		kind == .down && keyCode == KeyCode.escape
	}

	var isReturn: Bool {
		kind == .down && (keyCode == KeyCode.returnKey || keyCode == KeyCode.keypadEnter)
	}

	// A shortcut: the menu bar's, or the switcher's.
	var isShortcut: Bool {
		!modifiers.isDisjoint(with: [.command, .control])
	}
}

// What shows, and where the keyboard's focus is.
public struct KeyOverlays: Equatable, Sendable {
	// The PIN screen: the lock, or Settings' Set up, Change, and Remove.
	public var pinScreen = false
	public var quitConfirmation = false
	// Settings' App icon picker (ADR 0089).
	public var appIconPicker = false
	public var extensionPrompt = false
	public var extensionsMenu = false
	// An extension's more menu, beside the extensions menu.
	public var extensionMoreMenu = false
	public var switcher = false
	// A page's dialog: an alert, a permission, a sign-in.
	public var dialog = false
	public var pageFullscreen = false
	// A text field has the keyboard: a dialog's, or the address.
	public var textFieldFocused = false

	public init(
		pinScreen: Bool = false,
		quitConfirmation: Bool = false,
		appIconPicker: Bool = false,
		extensionPrompt: Bool = false,
		extensionsMenu: Bool = false,
		extensionMoreMenu: Bool = false,
		switcher: Bool = false,
		dialog: Bool = false,
		pageFullscreen: Bool = false,
		textFieldFocused: Bool = false
	) {
		self.pinScreen = pinScreen
		self.quitConfirmation = quitConfirmation
		self.appIconPicker = appIconPicker
		self.extensionPrompt = extensionPrompt
		self.extensionsMenu = extensionsMenu
		self.extensionMoreMenu = extensionMoreMenu
		self.switcher = switcher
		self.dialog = dialog
		self.pageFullscreen = pageFullscreen
		self.textFieldFocused = textFieldFocused
	}
}

// What a key does to an overlay.
public enum KeyCommand: Equatable, Sendable {
	case pinDigit(Character)
	case pinBackspace
	case pinBack
	case pinContinue
	case cancelQuit
	case quit
	case closeAppIconPicker
	case cancelExtensionPrompt
	case acceptExtensionPrompt
	case closeExtensionMoreMenu
	case closeExtensionsMenu
	case openSwitcher(backward: Bool)
	case cycleSwitcher(backward: Bool)
	case cancelSwitcher
	case commitSwitcher
	case cancelDialog
	case confirmDialog
	case exitPageFullscreen
}

public struct KeyRoute: Equatable, Sendable {
	public var command: KeyCommand?
	// Whether the key stops here: it reaches no window, menu, or page.
	public var consumed: Bool

	public init(_ command: KeyCommand?, consumed: Bool) {
		self.command = command
		self.consumed = consumed
	}

	static let pass = KeyRoute(nil, consumed: false)
	static let swallow = KeyRoute(nil, consumed: true)

	static func take(_ command: KeyCommand) -> KeyRoute {
		KeyRoute(command, consumed: true)
	}
}

public enum KeyRouting {
	public static func route(_ key: KeyPress, through overlays: KeyOverlays) -> KeyRoute {
		let layers: [(Bool, (KeyPress, KeyOverlays) -> KeyRoute?)] = [
			(overlays.pinScreen, pinScreen),
			(overlays.quitConfirmation, quitConfirmation),
			(overlays.appIconPicker, appIconPicker),
			(overlays.extensionPrompt, extensionPrompt),
			(overlays.extensionsMenu, extensionsMenu),
			(true, switcher),
			(overlays.dialog, dialog),
			(overlays.pageFullscreen, pageFullscreen),
		]

		for (shows, layer) in layers where shows {
			if let route = layer(key, overlays) {
				return route
			}
		}

		return .pass
	}

	// Each layer's answer: a route ends the key there, nil asks the next.

	// It takes every key but Quit: digits, Delete, Esc to go back, Return
	// to continue. Nothing reaches a page or a shortcut.
	private static func pinScreen(_ key: KeyPress, _: KeyOverlays) -> KeyRoute? {
		guard key.kind == .down else {
			return nil
		}

		if key.isQuit {
			return .pass
		}

		if key.isEscape {
			return .take(.pinBack)
		}

		if key.isReturn {
			return .take(.pinContinue)
		}

		if key.keyCode == KeyCode.delete {
			return .take(.pinBackspace)
		}

		if key.modifiers.isDisjoint(with: [.command, .control, .option]), let character = key.characters?.first {
			return .take(.pinDigit(character))
		}

		return .swallow
	}

	// As the PIN screen: Esc goes back, Return quits, and ⌘Q goes on to the
	// menu, which quits.
	private static func quitConfirmation(_ key: KeyPress, _: KeyOverlays) -> KeyRoute? {
		guard key.kind == .down else {
			return nil
		}

		if key.isQuit {
			return .pass
		}

		if key.isEscape {
			return .take(.cancelQuit)
		}

		if key.isReturn {
			return .take(.quit)
		}

		return .swallow
	}

	// Esc and Return close it (a sky applies when it is clicked); every other
	// key goes on to the window, where Tab and Space reach its icons.
	private static func appIconPicker(_ key: KeyPress, _: KeyOverlays) -> KeyRoute? {
		guard key.kind == .down, key.isEscape || key.isReturn else {
			return nil
		}

		return .take(.closeAppIconPicker)
	}

	// A popup takes all but shortcuts, as a dialog would.
	private static func extensionPrompt(_ key: KeyPress, _: KeyOverlays) -> KeyRoute? {
		guard key.kind == .down else {
			return nil
		}

		if key.isEscape {
			return .take(.cancelExtensionPrompt)
		}

		if key.isReturn {
			return .take(.acceptExtensionPrompt)
		}

		return key.isShortcut ? nil : .swallow
	}

	// Esc closes the more menu, then the menu; the rest goes on.
	private static func extensionsMenu(_ key: KeyPress, _ overlays: KeyOverlays) -> KeyRoute? {
		guard key.isEscape else {
			return nil
		}

		return .take(overlays.extensionMoreMenu ? .closeExtensionMoreMenu : .closeExtensionsMenu)
	}

	// Ctrl+Tab and Ctrl+Shift+Tab open it and step through it while Control
	// is held; releasing Control picks the highlighted tab, and Esc cancels.
	private static func switcher(_ key: KeyPress, _ overlays: KeyOverlays) -> KeyRoute? {
		switch key.kind {
		case .down where key.keyCode == KeyCode.tab && key.modifiers.contains(.control):
			let backward = key.modifiers.contains(.shift)

			return .take(overlays.switcher ? .cycleSwitcher(backward: backward) : .openSwitcher(backward: backward))

		case .down where key.isEscape && overlays.switcher:
			return .take(.cancelSwitcher)

		// Releasing Control: the window still sees it.
		case .modifiersChanged where overlays.switcher && !key.modifiers.contains(.control):
			return KeyRoute(.commitSwitcher, consumed: false)

		default:
			return nil
		}
	}

	// Esc dismisses it and Return confirms it, as Chrome's do. A text field
	// keeps its typing (the dialog's own, or the address); no other key
	// reaches the page under it, and shortcuts go on.
	private static func dialog(_ key: KeyPress, _ overlays: KeyOverlays) -> KeyRoute? {
		guard key.kind == .down else {
			return nil
		}

		if key.isEscape {
			return .take(.cancelDialog)
		}

		if key.isReturn {
			return .take(.confirmDialog)
		}

		return key.isShortcut || overlays.textFieldFocused ? nil : .swallow
	}

	// Esc leaves a page's fullscreen wherever focus is, as in Chrome.
	private static func pageFullscreen(_ key: KeyPress, _: KeyOverlays) -> KeyRoute? {
		key.isEscape ? .take(.exitPageFullscreen) : nil
	}
}
