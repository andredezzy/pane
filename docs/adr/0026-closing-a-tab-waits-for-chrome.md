# Closing a tab waits for Chrome

The app removes a loaded tab only when Chrome reports it closed, so a page's beforeunload can keep it, as in Chrome. Closing a tab (`UiApp::CloseTab`) closes its page in its Browser's tab strip as the user's gesture (`TabCloseTypes::CLOSE_USER_GESTURE`), which runs the page's beforeunload first, and a page that asks to stay shows its dialog (ADR 0021). The tab leaves the sidebar when the strip reports it removed (`UiApp::TabRemoved`), which it does only for a tab that closed (ADR 0016). A tab with no page goes at once.

Releasing a page closes it at once, with no beforeunload (`UiApp::Release`): a tab that sleeps, and every page of a profile that unloads, whose tabs stay in the sidebar (ADR 0041). Deleting a profile also closes its pages without asking them (ADR 0020), and so does a lock, as it closes each profile (ADR 0116). What would re-enter Chromium from inside its own notification waits for the next turn, and a page closed at once takes the requests it had pending with it (ADR 0017).

The `dialogs` scenario checks it: Cancel keeps the tab and its page, and Leave closes it.

Until 0.20.0 the Swift app closed a tab through the engine's API (`PaneWebContents -close`, or `-closeImmediately` at once) and heard the engine's `didRemoveTab`.
