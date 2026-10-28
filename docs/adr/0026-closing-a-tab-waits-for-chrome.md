# Closing a tab waits for Chrome

The app removes a loaded tab only when Chrome reports it closed, so a page's beforeunload can keep it, as in Chrome. Closing a tab (`PaneWebContents -close`, `engine/pane/public/PaneWebContents.h`) runs the page's beforeunload first, and a page that asks to stay shows its dialog (ADR 0021). The tab leaves the sidebar on the engine's `didRemoveTab`, which reports only a tab that closed (ADR 0016).

Unloading a profile closes its pages at once, with no beforeunload (`-closeImmediately`): the profile's tabs stay in the sidebar, and only their pages go. Deleting a profile also closes its pages without asking them (ADR 0020). Both calls wait for the next turn when made inside an engine callback (ADR 0017), and a page closed at once withdraws the requests the app holds for it (ADR 0074).

The `dialog-beforeunload` scenario checks it: Cancel keeps the tab and its page, and Leave closes it.
