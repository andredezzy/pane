# Extensions' keyboard shortcuts are changed in a dialog of the Extensions page

An extension's keyboard shortcuts (`chrome.commands`) can be changed and cleared, as in Chrome's chrome://extensions/shortcuts and Aside's Keyboard shortcuts. The Extensions page's header has a Keyboard shortcuts button beside the profile select, which opens a dialog over the window. Until now the shortcuts the extensions suggested worked (ADR 0098), and nothing could change them. Paper drew it first: 09.8 (the entry), 09.9 (the dialog), 09.10 (recording), 09.11 (Pane's own key), and 09.12 (the field's states), light and dark, approved by the user (ADR 0058).

- **The dialog** (`app/Pane/Extensions/ExtensionShortcutsView.swift`), in the App icon dialog's style: 560 points wide, rounded 29.
  - "Keyboard shortcuts", then "<Profile>'s extensions. Click a shortcut, then press the keys."
  - Each enabled extension with commands: its icon and name, then a row per command, its description beside its shortcut field. The action's command reads Chrome's "Activate the extension". A long list scrolls inside the dialog.
  - Done (↩) closes it, and so do Return, Esc while nothing records, and a click outside it.
  - The button is disabled while the profile has no extension enabled, which would leave the dialog empty.
- **The field:** 168 by 32, rounded 11, a 1-point border. The shortcut reads as the menu bar writes one, ⌃⌥⇧⌘ then the key, at 13 medium; Not set is muted.
  - Hover lays the foreground at 4% in 0.15 s and shows Clear on a set shortcut. A press shows 6%, and letting go starts recording.
  - Recording: the ring outside the border, Type a shortcut, or the modifiers held with a caret as they go down, and the esc hint. Esc, Tab (which leaves Chrome's field), a click beside the field, or the window losing key status (as Chrome's field ends one when it loses focus) stops it, the shortcut kept.
  - A key the field refuses leaves a message under it in the destructive color, the ring turns that color, and it keeps recording. Chrome's macOS messages and rules (`cr_shortcut_input.ts`): ⌥ with ⌃ or ⌘ is "Invalid combination"; a key without ⌃, ⌥, or ⌘ is "Include Ctrl, Alt, or ⌘"; modifiers with a key that can't end a shortcut are "Type a letter".
  - A key the menu bar holds stays Pane's (ADR 0098), and is refused naming the item: "⌘T is Pane's New Tab".
  - A key another command had moves here, as Chrome moves it; that field reads Not set, and this one says "Moved from <extension>" in muted text (the command's description when it was the same extension's).
  - A message comes and goes as the dialog grows and shrinks around it, in 0.15 s: the key that caused it was typed.
- **Every shortcut is paused while a field records,** as Chrome's page suspends them. The key monitor (`KeyRouting`) gives the field every key, the menu bar's and Quit's included, and the modifiers as they change. The engine's registry is suspended too.
- **The engine,** through Chrome's `CommandService`, as `developerPrivate` uses it for the shortcuts page:
  - `PaneBrowserContext.commandsOfExtension:` lists an extension's commands as `ExtensionInfoGenerator`'s `ConstructCommands` does, the action's first, each with its active shortcut as `Command::AcceleratorToString` writes it ("Command+Shift+Y").
  - `setShortcut:ofCommand:extension:` is `UpdateKeybindingPrefs`, as `developerPrivate.updateExtensionCommand` calls it, an empty shortcut clearing it as Chrome's Clear does. It returns the extension whose command lost the key, which `CommandService` reports as it removes it.
  - `setExtensionShortcutsSuspended:inWindow:` is `ExtensionKeybindingRegistry::SetShortcutHandlingSuspended` on the window's registry (`pane::ExtensionShortcuts`), which `runExtensionShortcut:inWindow:` then honors.
  - `+extensionShortcutForEvent:` writes a key down as Chrome would (`KeyboardCodeFromNSEvent`, `AcceleratorToString`), and only what Chrome's parse takes back (`Command::StringToAccelerator`), so what is recorded is what the registry matches.
  - The app turns Chrome's string into the Mac's glyphs (`ExtensionShortcut`, PaneKit).

What was rejected:

- **Chrome's chrome://extensions/shortcuts itself:** Chrome's WebUI, which Pane does not show (ADR 0021).
- **The scope select (In Chrome, Global):** the approved design leaves it out. Every shortcut works in Pane's window, as ADR 0098 has them.
- **⌃⇧⌘ with a key, which Chrome's field takes:** Chrome's parse allows two modifiers at most and clears the shortcut instead. The field refuses it as "Invalid combination".
- **Chrome's field's look while recording:** it shows no keys until one sets, and refuses ⌥⌘ as soon as both go down. The board shows the modifiers held, and the rules apply once a key that is not a modifier comes.
- **Clearing with `RemoveKeybindingPrefs`:** an update of the extension would bring its suggested key back. Chrome's Clear goes through `UpdateKeybindingPrefs`, which keeps the command the user's.

How it is checked:

- **`ExtensionShortcutTests` and `KeyRoutingTests` (PaneKit):** the glyphs, Chrome's rules and messages, and the dialog's keys: Esc and Return close it, and a field recording takes every key but Esc and Tab, which stop it.
- **The `extension-shortcuts` scenario,** with a second probe (`tests/pages/extensions/commands-second`) suggesting ⌥⇧K. Keys are real key presses through the automation.
  - The automation's `extensionShortcuts` lists, through the dialog's own controller method, the probe's Activate the extension (⇧⌘U) and Run the probe's command (⇧⌘Y), and the second probe's ⌥⇧K.
  - A real click on a field records. A key alone, ⌥⌘Y, ⌘-, and ⌘T each leave their message, the field recording and the shortcut kept; ⌘T opens no tab.
  - ⌃⇧Y sets the probe's command. ⌥⇧K then moves from the second probe, which reads Not set, and the dialog says so. Clear empties the action's shortcut. Esc stops a recording, then closes the dialog.
  - On the page, ⌥⇧K runs the probe's command and not the second probe's; ⌘⇧Y, ⌃⇧Y, and the cleared ⌘⇧U run nothing.
  - While a field records, ⌥⇧K is typed into it rather than run, and the trace has the registry suspended.
- **On screen** (`render`, 2026-10-30): the dialog at rest, light and dark, pressed, recording, with ⌥⌘ held, with each message, light and dark for Pane's own key, after a set, and after a move, light and dark. Measured against 09.9: rows 40 points, an extension's line 28, extensions 16 apart, the dialog 560 wide. The hover was not rendered: it follows the real pointer, which a posted move does not stand in for, and the Mac was in use.
- **The `extension-details`, `extension-context-menus`, `extensions-menu`, `extension-management`, `shortcuts`, `key-precedence`, and `smoke` scenarios pass.**
