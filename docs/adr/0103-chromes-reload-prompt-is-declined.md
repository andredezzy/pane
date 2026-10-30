# Chrome's reload prompt after a site access change is declined without a dialog

When an extension's site access changes on a page, Chrome asks to reload it: "Reload this page to apply your updated settings on this site", with Reload and Cancel. Pane's tabs show no Chrome dialog (ADR 0021). Choosing When You Click the Extension in the action's menu (ADR 0101) put Chrome's own sheet on Pane's window, so Pane now declines the prompt at once, as it declines the device choosers (ADR 0022). The page keeps what it has and takes the change at its next load.

- **The hook:** `g_pane_reload_page_dialog` (`engine/pane/browser/extensions/extension_hooks.h`, patch `0005-pane-extensions`) sits in `ExtensionActionRunner::ShowReloadPageBubble`, the one place Chrome shows the prompt from. It serves both a site access change (`SitePermissionsHelper`) and a click on an action whose blocked scripts need a reload.
- **For Pane's tabs** (`HostedBrowserWindow::IsOurs`), it answers Cancel and traces `PaneExtensions.declineReloadPrompt`. Another Browser's tab keeps Chrome's prompt.

What was rejected:

- **Chrome's sheet:** Chrome's UI on Pane's window, which ADR 0021 rules out.
- **Reloading at once:** it would throw away what the page holds, such as a form being filled, without asking.
- **A reload prompt of Pane's own:** it would be a new design, which Paper draws first (ADR 0058). Until one is drawn and approved, the change waits for the page's next load.

How it is checked:

- **The `extension-context-menus` scenario:** after When You Click the Extension, the trace has `PaneExtensions.declineReloadPrompt`, and no window but the main one shows. Before the hook, Chrome's sheet showed there.
