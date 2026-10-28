# Print uses the system print panel

Printing goes straight to the system print panel, from ⌘P and from a page's `window.print()`, and never to Chrome's print preview: the engine starts Chrome with `--disable-print-preview` (`engine/pane/browser/browser_application.mm`), so printing shows the Mac's own panel and no Chrome UI (ADR 0021). The engine tells the app as a page starts printing (`webContentsWillPrint:`, through the hook on `PrintViewManagerBase::ScriptedPrint` in `engine/patches/0004-pane-ui-hooks.patch`), which only the scenarios read. Behind the PIN lock the menu's Print… does not run (ADR 0042).

The `print` scenario opens the panel from `window.print()` and from ⌘P and cancels it, and the page goes on.
