# Extensions' own menu items show where Chrome shows them

The items an extension adds with `chrome.contextMenus` show in the page's right-click menu and in its pinned action's, laid out by Chrome's own `ContextMenuMatcher`. The order, the grouping (an extension with several items gets one submenu under its name), titles cut at 75 characters with the selection in them, radio groups, separators, and the action menu's limit of six top-level items are all Chrome's. Until now neither menu showed them: an extension such as a password manager that saves or fills from the right-click menu had no way in.

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

What was rejected:

- **Matching the items in the app from `MenuManager`'s list:** it would repeat Chrome's matching, grouping, and limits, and drift from them.
- **Chrome's own `RenderViewContextMenu`:** in host mode Chrome shows no UI of its own (ADR 0021), and its model carries Chrome's items, which Pane draws itself.
- **Listing the action's items in the extensions menu's more menu too:** Chrome's menu there does, but Aside's, which Pane's follows (ADR 0054), does not, and Pane's popover would need submenus and checkboxes. An unpinned extension's action items show once it is pinned.
- **Site access (This Can Read and Change Site Data, View Web Permissions):** it still needs the engine (ADR 0054). Paper draws it, and the app does not show it yet.
- **Checking the action's menu through accessibility or the menu itself:**
  - On macOS 27, SwiftUI draws a context menu out of process: the app holds an `NSCampoMenuItem` with a remote view, and accessibility cannot resolve the menu's window.
  - A scenario reads the items from what the menu reads instead.

How it is checked:

- **The `extension-context-menus` scenario,** with a probe extension (`tests/pages/extensions/context-menus`) that adds two page items and two action items:
  - **The page's menu** lists Back, Forward, Reload, the probe's submenu, then Inspect, and the submenu holds Probe page item and Probe page check. Choosing each runs `onClicked` with the page's URL and its tab. The checkbox shows checked the next time, and choosing it again clears it.
  - **The action's items** come through `ExtensionsController.actionMenu`, the call the menu makes (`extensionActionMenu`): Probe action item, then Probe action check, checked. Choosing the first runs `onClicked` for the page's tab, with no page.
- **On screen** (2026-10-29), after a real right-click through cua-driver:
  - The action's menu showed its name, the two items (the second checked), Remove from Pane, Unpin, and Manage Extension.
  - The page's menu showed the probe's submenu between separators, before Inspect.
- **The menu's cost:** the engine traces how long it takes to make a page's menu (`madeMs` on `showContextMenu`).
  - With no extension items it takes 0.014 to 0.035 ms (the 8 menus of `context-menu`).
  - With the probe's items it takes 0.09 to 0.11 ms, and 0.35 ms the first time: well inside a frame.
- **The `context-menu`, `extensions-menu`, `extensions-page-profile`, `tabs-api`, and `smoke` scenarios** pass.
