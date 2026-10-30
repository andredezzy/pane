# The pinned action's right-click menu offers Chrome's site access

A pinned action's right-click menu lists the extension's site access, as Chrome's and Aside's do: This Can Read and Change Site Data, a submenu of When You Click the Extension, On <site>, and On All Sites, the current one checked, then Learn More About Site Access. Where the extension can't run on the page, the menu reads Can't Read or Change Site's Data, disabled, instead. View Web Permissions follows Manage Extension. Both rows show only for an extension that asks for site access, by host permissions or activeTab. Until now the menu left site access out, since it needed the engine (ADR 0054). Paper drew it first: 04.13.1 and 04.13.2, light and dark, approved by the user (ADR 0058).

- **The menu:** its name; the extension's own items (ADR 0096); its site access; Remove from Pane and Unpin; then Manage Extension and View Web Permissions. A separator stands between each group.
- **The engine:** `PaneExtensionActionMenu.siteAccessItems` (`engine/pane/browser/extensions/extension_menu.mm`) are Chrome's page access items, as `ExtensionContextMenuModel::CreatePageAccessItems` makes them on the path without `kExtensionsMenuAccessControl`. That is the menu Aside shows on Chromium 154.
  - Each item carries Chrome's title-case strings and command IDs, with `PermissionsManager` deciding which choices show, which are enabled, and which is checked, for the page's origin.
  - The site is the host without www, as Chrome's `GetCurrentSite`.
  - The apostrophes are typographic, as the app's own text and the boards set them. Chrome's strings use straight ones.
- **Choosing one:** `runItem:` runs it as Chrome's model does. A choice goes through `SitePermissionsHelper::UpdateSiteAccess`, while the tab is still at the origin the menu was made for, and only for an extension whose access the user can change. Learn More opens Chrome's help article (`kRuntimeHostPermissionsHelpURL`, support.google.com/chrome?p=enable_extensions) in a new tab of the window, as Chrome's `OpenUrl` does. Chrome's reload prompt after a choice is ADR 0103's.
- **View Web Permissions and Manage Extension:** both open the extension's Details on the Extensions page (ADR 0102), as Chrome opens chrome://extensions/?id=. Chrome's View Web Permissions opens the extension's site settings, which Pane does not have; Details lists its permissions and site access.
- **Fresh:** the engine reports a change of an extension's permissions (`ExtensionsBridge` observes `PermissionsManager`), so the menu, and Details, show a choice the next time.

What was rejected:

- **Chrome's `ExtensionContextMenuModel` itself:** it needs a Browser with Chrome's toolbar, and it carries Chrome's other items (Options, Remove with Chrome's dialog, the side panel, Rate it), which Pane's menu draws itself or leaves out.
- **The kExtensionsMenuAccessControl wording** (Ask on Every Visit, Always on <site>): Chrome keeps that feature off on the desktop, and Aside shows the classic wording.
- **Straight apostrophes:** the boards, and every string of the app's, set typographic ones.

How it is checked:

- **The `extension-context-menus` scenario:** its probe (`tests/pages/extensions/context-menus`) now asks for every site.
  - The action's site access reads the submenu with On All Sites checked, and Learn More after the choices.
  - When You Click the Extension leaves the probe without `<all_urls>`, as `chrome.permissions.getAll` reports it. On 127.0.0.1 grants that site alone, and On All Sites gives every site back. After each, the submenu shows the choice checked.
  - On a data: page the menu reads Can't Read or Change Site's Data, disabled.
- **On screen** (2026-10-30, a real right-click through cua-driver): the menu showed the name, the probe's two items, This Can Read and Change Site Data with its submenu, Remove from Pane, Unpin, Manage Extension, and View Web Permissions, in Paper's order. After the page moved to another host, the submenu named it.
- **The `extensions-menu` scenario:** Manage Extension opens Details.
