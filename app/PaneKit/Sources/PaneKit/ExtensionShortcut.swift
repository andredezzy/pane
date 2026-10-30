// An extension's keyboard shortcut (chrome.commands) as Chrome writes it,
// "Command+Shift+Y" (Command::AcceleratorToString), shown as the menu bar
// writes one: ⌃⌥⇧⌘, then the key. And Chrome's rules for a key typed into
// the Keyboard shortcuts dialog's field (cr_shortcut_input.ts, on macOS),
// with the engine's own (Command::StringToAccelerator) and Pane's: a key the
// menu bar holds stays Pane's (ADR 0098).
public enum ExtensionShortcut {
	// Why the field did not take a key; it keeps recording.
	public enum Refusal: Equatable, Sendable {
		// ⌥ with ⌃ or ⌘, or three modifiers, which Chrome's parse refuses.
		case invalidCombination
		// No ⌃, ⌥, or ⌘.
		case includeModifier
		// Modifiers with a key that can't end a shortcut.
		case typeALetter
		// The menu bar's item with the key, and the key.
		case panes(item: String, shortcut: String)

		// Chrome's own macOS strings, and Pane's.
		public var message: String {
			switch self {
			case .invalidCombination: "Invalid combination"
			case .includeModifier: "Include Ctrl, Alt, or ⌘"
			case .typeALetter: "Type a letter"
			case let .panes(item, shortcut): "\(ExtensionShortcut.glyphs(shortcut)) is Pane’s \(item)"
			}
		}
	}

	// What the modifiers of a key down rule out, in Chrome's order, before
	// its key is read: too many first, then none.
	public static func refusal(_ modifiers: KeyPress.Modifiers) -> Refusal? {
		let count = [KeyPress.Modifiers.control, .option, .shift, .command].filter(modifiers.contains).count

		if modifiers.contains(.option) && !modifiers.isDisjoint(with: [.control, .command]) {
			return .invalidCombination
		}

		// ⌃⇧⌘: Chrome's field takes it, but its parse allows two modifiers
		// and would clear the shortcut instead.
		if count > 2 {
			return .invalidCombination
		}

		if modifiers.isDisjoint(with: [.control, .option, .command]) {
			return .includeModifier
		}

		return nil
	}

	// "Command+Shift+Y" as ⇧⌘Y; empty stays empty.
	public static func glyphs(_ shortcut: String) -> String {
		var modifiers = KeyPress.Modifiers()
		var key = ""

		for token in shortcut.split(separator: "+") {
			switch token {
			case "Ctrl": modifiers.insert(.control)
			case "Alt": modifiers.insert(.option)
			case "Shift": modifiers.insert(.shift)
			case "Command": modifiers.insert(.command)
			default: key = keyGlyphs[String(token)] ?? String(token)
			}
		}

		return glyphs(modifiers) + key
	}

	// Held modifiers, in the Mac's order.
	public static func glyphs(_ modifiers: KeyPress.Modifiers) -> String {
		[(KeyPress.Modifiers.control, "⌃"), (.option, "⌥"), (.shift, "⇧"), (.command, "⌘")]
			.filter { modifiers.contains($0.0) }
			.map(\.1)
			.joined()
	}

	// Chrome's names for the keys that aren't a letter or a digit, as the
	// menu bar draws them.
	private static let keyGlyphs = [
		"Comma": ",",
		"Period": ".",
		"Up": "↑",
		"Down": "↓",
		"Left": "←",
		"Right": "→",
		"Insert": "Insert",
		"Delete": "⌦",
		"Home": "↖",
		"End": "↘",
		"PageUp": "⇞",
		"PageDown": "⇟",
		"Space": "Space",
		"Tab": "⇥",
		"MediaNextTrack": "⏭",
		"MediaPlayPause": "⏯",
		"MediaPrevTrack": "⏮",
		"MediaStop": "⏹",
	]
}
