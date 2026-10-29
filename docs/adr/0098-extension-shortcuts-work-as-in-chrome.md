# Extensions' keyboard shortcuts work as in Chrome

An extension's keyboard shortcuts (`chrome.commands`) work as in Chrome and in Aside. A command's shortcut sends `onCommand` with the active tab, and the action's (`_execute_action`) works as a click on the action: its popup, or `action.onClicked`. They work with the page focused, and with the address field being edited. Until now no key reached them. Comparing Pane's Extensions page with Aside's (the user's request: keep parity with stable browsers) found them missing, and the probe extension confirmed it.

- **The engine:** `pane::ExtensionShortcuts` (`engine/pane/browser/extensions/extension_shortcuts.mm`) is Chrome's `ExtensionKeybindingRegistry`, one per Browser, made when the Browser attaches to its window (`HostedBrowserWindow`).
  - It keeps every enabled extension's active shortcuts as Chrome's views registry does, and the action's beside them. Chrome's toolbar keeps the action's itself, and Pane's app has no toolbar views.
  - `PaneBrowserContext`'s `runExtensionShortcut:inWindow:` runs one.
  - A command goes through the registry, which grants the active tab, as Chrome does. The action goes through `runActionOfExtension:inWindow:`, the toolbar's click.
- **The app:** `KeyRouter`, the one key monitor, sees each key before any view, the page's included. A key the overlays leave goes to `ExtensionsController.runShortcut` when it is a chord (⌘, ⌃, or ⌥), for the active profile. A shortcut goes before the page, as in Chrome.
- **What stays the browser's:**
  - A key the menu bar holds stays the menu's, as Chrome's own shortcuts stay Chrome's.
  - The lock screen, Quit Pane?, and the other overlays take their keys first. Behind the lock no extension shortcut runs.

What was rejected:

- **The page's pre-handling alone** (`HostedBrowserWindow::PreHandleKeyboardEvent`): it sees only keys sent to a page, so a shortcut typed in the address field would do nothing. In Chrome and Aside it works there too.
- **Chrome's views registry** (`ExtensionKeybindingRegistryViews`): it registers with a views `FocusManager`, which Pane's windows do not have.
- **A page to change the shortcuts, as Aside's Keyboard shortcuts** (Chrome's `chrome://extensions/shortcuts`): it is not built yet. The shortcuts the extensions suggest work, and Chrome's own rules settle the conflicts between extensions.

How it is checked:

- **The `extension-shortcuts` scenario,** with a probe extension (`tests/pages/extensions/commands`) that suggests ⌘⇧Y for a command and ⌘⇧U for its action. Each is a real key press through the automation.
  - With the page focused, ⌘⇧Y sends `onCommand` with the active tab, and ⌘⇧U runs the action for the same tab.
  - With the address field being edited, ⌘⇧Y sends `onCommand` too.
- **The cost:** a chord asks the engine to look up a map, and walks the menu bar only when the key is an extension's shortcut.
- **Other scenarios:**
  - `shortcuts`, `extensions-menu`, `extension-context-menus`, `tabs-api`, `security`, and `smoke` pass.
  - `key-precedence` passed three runs in a row after one run timed out waiting for the window to be key while the Mac was in use.
