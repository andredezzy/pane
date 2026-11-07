# Extensions' own menu items and site access show where Chrome shows them, and Chrome's reload prompt after a change is declined without a dialog

**An extension's own items.** The items an extension adds with `chrome.contextMenus` show in the page's right-click menu and in its pinned action's, laid out by Chrome's own `ContextMenuMatcher`. The order, the grouping (an extension with several items gets one submenu under its name), titles cut at 75 characters with the selection in them, radio groups, separators, and the action menu's limit of six top-level items are all Chrome's. Until then neither menu showed them: an extension such as a password manager that saves or fills from the right-click menu had no way in.

- **The engine:** `pane::ExtensionMenu` (`engine/pane/browser/extensions/extension_menu.mm`) fills a `ui::SimpleMenuModel` through the matcher.
  - For a page, it takes every extension's items that match what was clicked (`PopulateExtensionItems` with `MenuItemMatchesParams`, as `RenderViewContextMenu`).
  - For an action, it takes the extension's items in its action's context, through a copy of Chrome's `MenuItemMatchesAction`, which is private to `extension_context_menu_model.cc`.
  - The app gets the model as `PaneExtensionMenuItem`s: `PaneContextMenu.extensionItems`, and `PaneBrowserContext`'s `actionMenuOfExtension:inWindow:`, which is nil when the extension has none.
  - An item runs through the matcher, so `MenuManager` updates a checkbox's or a radio group's state and sends `onClicked`. A page item sends it with the page, frame, and tab; an action item sends it, as Chrome's does, with the active tab and no page.
- **The app:**
  - `PageContextMenu` puts the items after the page's own, between separators, before Inspect.
  - The action's menu (`ExtensionActionsView`) puts them after its name.
  - A checkbox or radio item shows its check, and a submenu opens as one.
- **Icons:** the engine gives a top-level item the extension's icon, as Chrome does. On macOS 27 AppKit hides a menu item's image unless the item asks for it (`preferredImageVisibility`), and Pane leaves the choice to macOS, as for every menu. So the item reads as the extension's name, or its item's own title.

**Its site access.** A pinned action's right-click menu lists the extension's site access, as Chrome's and Aside's do: This Can Read and Change Site Data, a submenu of When You Click the Extension, On <site>, and On All Sites, the current one checked, then Learn More About Site Access. Where the extension can't run on the page, the menu reads Can't Read or Change Site's Data, disabled, instead. View Web Permissions follows Manage Extension. Both rows show only for an extension that asks for site access, by host permissions or activeTab. Until then the menu left site access out, since it needed the engine (ADR 0054). Paper drew it first: 04.13.1 and 04.13.2, light and dark, approved by the user (ADR 0058).

- **The menu:** its name; the extension's own items; its site access; Remove from Pane and Unpin; then Manage Extension and View Web Permissions. A separator stands between each group.
- **The engine:** `PaneExtensionActionMenu.siteAccessItems` (`engine/pane/browser/extensions/extension_menu.mm`) are Chrome's page access items, as `ExtensionContextMenuModel::CreatePageAccessItems` makes them on the path without `kExtensionsMenuAccessControl`. That is the menu Aside shows on Chromium 154.
  - Each item carries Chrome's title-case strings and command IDs, with `PermissionsManager` deciding which choices show, which are enabled, and which is checked, for the page's origin.
  - The site is the host without www, as Chrome's `GetCurrentSite`.
  - The apostrophes are typographic, as the app's own text and the boards set them. Chrome's strings use straight ones.
- **Choosing one:** `runItem:` runs it as Chrome's model does. A choice goes through `SitePermissionsHelper::UpdateSiteAccess`, while the tab is still at the origin the menu was made for, and only for an extension whose access the user can change. Learn More opens Chrome's help article (`kRuntimeHostPermissionsHelpURL`, support.google.com/chrome?p=enable_extensions) in a new tab of the window, as Chrome's `OpenUrl` does.
- **View Web Permissions and Manage Extension:** both open the extension's Details on the Extensions page (ADR 0062), as Chrome opens chrome://extensions/?id=. Chrome's View Web Permissions opens the extension's site settings, which Pane does not have; Details lists its permissions and site access.
- **Fresh:** the engine reports a change of an extension's permissions (`ExtensionsBridge` observes `PermissionsManager`), so the menu, and Details, show a choice the next time.
- **Chrome's reload prompt is declined.** When an extension's site access changes on a page, Chrome asks to reload it: "Reload this page to apply your updated settings on this site", with Reload and Cancel. Choosing When You Click the Extension put Chrome's own sheet on Pane's window, where Pane's tabs show no Chrome dialog (ADR 0021). Pane declines the prompt at once, as it declines the device choosers (ADR 0022): the page keeps what it has and takes the change at its next load.
  - **The seam:** `ReloadPageDialogController::AcceptDialogForTesting(false)`, held for the process (`engine/pane/browser/extensions/extension_ui.mm`), answers Cancel in `ExtensionActionRunner::ShowReloadPageBubble`, the one place Chrome shows the prompt from. It serves both a site access change (`SitePermissionsHelper`) and a click on an action whose blocked scripts need a reload. It replaced a hook of Pane's in `extension_action_runner.cc` in the review of 0.18.0, under ADR 0004's rule; a test-named API that goes away is a compile error at an upgrade, as a conflict would be. Chrome's "added" bubble goes the same way (`ExtensionInstallUI::disable_ui_for_tests`).

What was rejected:

- **Matching the items in the app from `MenuManager`'s list:** it would repeat Chrome's matching, grouping, and limits, and drift from them.
- **Chrome's own `RenderViewContextMenu`:** in host mode Chrome shows no UI of its own (ADR 0021), and its model carries Chrome's items, which Pane draws itself.
- **Listing the action's items in the extensions menu's more menu too:** Chrome's menu there does, but Aside's, which Pane's follows (ADR 0054), does not, and Pane's popover would need submenus and checkboxes. An unpinned extension's action items show once it is pinned.
- **Site access in the items' change:** it needed the engine to expose the extension's site access (ADR 0054), and shipped next.
- **Checking the action's menu through accessibility or the menu itself:**
  - On macOS 27, SwiftUI draws a context menu out of process: the app holds an `NSCampoMenuItem` with a remote view, and accessibility cannot resolve the menu's window.
  - A scenario reads the items from what the menu reads instead.
- **Chrome's `ExtensionContextMenuModel` itself:** it needs a Browser with Chrome's toolbar, and it carries Chrome's other items (Options, Remove with Chrome's dialog, the side panel, Rate it), which Pane's menu draws itself or leaves out.
- **The kExtensionsMenuAccessControl wording** (Ask on Every Visit, Always on <site>): Chrome keeps that feature off on the desktop, and Aside shows the classic wording.
- **Straight apostrophes:** the boards, and every string of the app's, set typographic ones.
- **Chrome's reload sheet:** Chrome's UI on Pane's window, which ADR 0021 rules out.
- **Reloading at once:** it would throw away what the page holds, such as a form being filled, without asking.
- **A reload prompt of Pane's own:** it would be a new design, which Paper draws first (ADR 0058). Until one is drawn and approved, the change waits for the page's next load.

How it is checked:

- **The `extension-context-menus` scenario,** with a probe extension (`tests/pages/extensions/context-menus`) that adds two page items and two action items, and asks for every site:
  - **The page's menu** lists Back, Forward, Reload, the probe's submenu, then Inspect, and the submenu holds Probe page item and Probe page check. Choosing each runs `onClicked` with the page's URL and its tab. The checkbox shows checked the next time, and choosing it again clears it.
  - **The action's items** come through `ExtensionsController.actionMenu`, the call the menu makes (`extensionActionMenu`): Probe action item, then Probe action check, checked. Choosing the first runs `onClicked` for the page's tab, with no page.
  - **The action's site access** reads the submenu with On All Sites checked, and Learn More after the choices. When You Click the Extension leaves the probe without `<all_urls>`, as `chrome.permissions.getAll` reports it. On 127.0.0.1 grants that site alone, and On All Sites gives every site back. After each, the submenu shows the choice checked. On a data: page the menu reads Can't Read or Change Site's Data, disabled.
  - **The reload prompt:** after When You Click the Extension, no window but the main one shows. Before the answer, Chrome's sheet showed there.
- **On screen,** after a real right-click through cua-driver:
  - The action's menu (2026-10-29) showed its name, the two items (the second checked), Remove from Pane, Unpin, and Manage Extension.
  - The page's menu showed the probe's submenu between separators, before Inspect.
  - With site access (2026-10-30), the menu showed the name, the probe's two items, This Can Read and Change Site Data with its submenu, Remove from Pane, Unpin, Manage Extension, and View Web Permissions, in Paper's order. After the page moved to another host, the submenu named it.
- **The menu's cost:** the engine traces how long it takes to make a page's menu (`madeMs` on `showContextMenu`).
  - With no extension items it takes 0.014 to 0.035 ms (the 8 menus of `context-menu`).
  - With the probe's items it takes 0.09 to 0.11 ms, and 0.35 ms the first time: well inside a frame.
- **The `extensions-menu` scenario:** Manage Extension opens Details.
- **The `context-menu`, `extensions-page-profile`, `tabs-api`, and `smoke` scenarios** pass.
