# ⌘Q asks first

⌘Q asks before Pane quits, as Aside does (the user's request). Quit Pane?, designed in Paper first (`03 · Browsing` › 03.5 Quit Pane, light and dark, and Quit confirmation in `02 · Components` › 02.5 Overlays), sits over the window on a light veil, black 14% (45% on dark): a 40-point icon tile with a dashed circle-x, the title, what quitting does (profiles and tabs come back; downloads in progress stop), then Cancel with its Esc key and the destructive Quit with ⌘Q (`app/Pane/Views/QuitConfirmationView.swift`).

⌘Q again, Return, or Quit quits; Esc, Cancel, or a click outside goes back. While it shows it takes every key, as the PIN screen does, so none reaches the page whatever has focus, and it passes ⌘Q on to the menu; the one key monitor routes them (ADR 0079). At the PIN screen ⌘Q quits at once: nothing is open to lose there.

It replaces Chrome's own hold-to-quit. Chrome's Warn Before Quitting pref is on by default and asks for a held ⌘Q in a window of its own; the engine turns it off (`kConfirmToQuitEnabled`). The `quit` scenario drives Quit Pane? through the app's real key path, and `hosted` chooses Quit Pane twice.
