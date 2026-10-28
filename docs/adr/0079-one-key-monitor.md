# One key monitor, in drawing order

Pane has one key monitor, and it offers each key to the overlays that show in the order they are drawn, topmost first, so the overlay on top answers a key before the one under it. Before, the PIN screen's, Quit Pane?'s, the extensions', and the switcher's monitors each decided alone, in an order AppKit does not document.

`KeyRouter` (`app/Pane/Browser/KeyRouter.swift`) is the one monitor, a local event monitor: it sees every key before any view, a page included, and before the menu bar's shortcuts. PaneKit's `KeyRouting` decides, as a pure function of what shows: the PIN screen (ADR 0042), Quit Pane? (ADR 0053), an extension's popup, the extensions menu, the switcher, a page's dialog (ADR 0080), a page's fullscreen. The first that takes a key ends it. Key codes are named (`KeyCode`), and 13 tests cover the routing.

`key-precedence` opens the extensions menu and Quit Pane? over it, in both orders, and presses Esc twice. Before, with the menu first, one Esc closed the menu hidden under the panel and Quit Pane? stayed; now Quit Pane? goes first, then the menu. This was A4 of the architecture review of 2026-10-28.
