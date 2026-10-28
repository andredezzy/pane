# Quit outranks a remote debugging client

Quitting Pane quits it even while a remote debugging client is attached: Pane's windows are the app's, and Quit is the user's call. Chrome started without a window (`--no-startup-window`, which host mode passes, ADR 0010) and with `--remote-debugging-port` holds a keep-alive for the client until the client closes the browser. Pane's quit releases it: as the app terminates, the engine calls `ChromeDevToolsManagerDelegate::AllowBrowserToClose` (`engine/pane/browser/browser_main_extra_parts.mm`).

Pane launched normally has no such keep-alive. The case is the scenarios', which launch the app with remote debugging, and the runs on an unlocked screen found Quit Pane not quitting there. On an unlocked screen, the `hosted` scenario chooses Quit Pane from the menu, twice since ⌘Q asks first (ADR 0053), and expects the app and every helper to be gone within 5 seconds.
