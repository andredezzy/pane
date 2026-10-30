import PaneKit
import Testing

@Suite("extension shortcuts")
struct ExtensionShortcutTests {
	@Test("Chrome's shortcut reads in the Mac's order, the key last")
	func glyphs() {
		#expect(ExtensionShortcut.glyphs("Command+Shift+Y") == "⇧⌘Y")
		#expect(ExtensionShortcut.glyphs("Ctrl+Alt+Shift+Command+K") == "⌃⌥⇧⌘K")
		#expect(ExtensionShortcut.glyphs("Alt+Shift+Comma") == "⌥⇧,")
		#expect(ExtensionShortcut.glyphs("Ctrl+Up") == "⌃↑")
		#expect(ExtensionShortcut.glyphs("Command+Space") == "⌘Space")
		#expect(ExtensionShortcut.glyphs("Command+7") == "⌘7")
		#expect(ExtensionShortcut.glyphs("") == "")
	}

	@Test("held modifiers read in the same order")
	func heldModifiers() {
		#expect(ExtensionShortcut.glyphs([.command, .option]) == "⌥⌘")
		#expect(ExtensionShortcut.glyphs([.shift, .control, .capsLock]) == "⌃⇧")
		#expect(ExtensionShortcut.glyphs([]) == "")
	}

	@Test("a key alone needs Ctrl, Alt, or ⌘; Shift is not one")
	func includeModifier() {
		#expect(ExtensionShortcut.refusal([]) == .includeModifier)
		#expect(ExtensionShortcut.refusal([.shift]) == .includeModifier)
		#expect(ExtensionShortcut.refusal([.capsLock]) == .includeModifier)
		#expect(ExtensionShortcut.refusal([.command, .shift]) == nil)
		#expect(ExtensionShortcut.refusal([.control]) == nil)
		#expect(ExtensionShortcut.refusal([.option, .shift]) == nil)
		#expect(ExtensionShortcut.refusal([.control, .command]) == nil)
	}

	@Test("⌥ with ⌃ or ⌘, or three modifiers, is an invalid combination")
	func invalidCombination() {
		#expect(ExtensionShortcut.refusal([.option, .command]) == .invalidCombination)
		#expect(ExtensionShortcut.refusal([.option, .control]) == .invalidCombination)
		#expect(ExtensionShortcut.refusal([.control, .option, .shift, .command]) == .invalidCombination)
		#expect(ExtensionShortcut.refusal([.control, .shift, .command]) == .invalidCombination)
	}

	@Test("the messages are Chrome's macOS strings, and Pane's for its own keys")
	func messages() {
		#expect(ExtensionShortcut.Refusal.includeModifier.message == "Include Ctrl, Alt, or ⌘")
		#expect(ExtensionShortcut.Refusal.invalidCombination.message == "Invalid combination")
		#expect(ExtensionShortcut.Refusal.typeALetter.message == "Type a letter")
		#expect(ExtensionShortcut.Refusal.panes(item: "New Tab", shortcut: "Command+T").message == "⌘T is Pane’s New Tab")
	}
}
