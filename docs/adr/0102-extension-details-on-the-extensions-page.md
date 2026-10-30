# An extension's Details are a view of the Extensions page

Each extension has its Details, as chrome://extensions and Aside's extensions page have theirs: a view inside the Extensions page, with a back arrow to the list. A card's Details button opens it, and so do Manage Extension and View Web Permissions (ADR 0101), as Chrome opens chrome://extensions/?id=. Until now Pane showed only the card, with no permissions, site access, or file access, and Manage Extension scrolled the list to the card. Paper drew it first: 09.13, 09.14 (the site access pop-up open), and 09.15 (the card before and after), light and dark, approved by the user (ADR 0058).

- **The view** (`app/Pane/Extensions/ExtensionDetailsView.swift`), in Settings' layout (ADR 0065) and 640 points wide at most:
  - **The header:** the back arrow, the 40-point icon, the name over "Version X · <size> · <source>", and the switch.
  - **The description**, at 13 on 20-point lines.
  - **Permissions:** the extension's permission messages in Chrome's words, or Chrome's "This extension requires no special permissions".
  - **Site access:** "Allow this extension to read and change all your data on websites you visit", with the pop-up On click, On specific sites, and On all sites.
  - **Pin to toolbar**, for an extension with an action, and **Allow access to file URLs**, for one that wants file access.
  - **Links:** Extension options (while the extension is enabled, as Chrome's), Open extension website (the manifest's homepage_url), and View in Chrome Web Store (for an extension that updates from the store), each opening a new tab of the profile.
  - **Remove extension:** "Removes it from <profile> only.", with Remove, which asks first in the popup it always has (ADR 0056).
- **The card** loses its version, which Details shows, and leads its buttons with Details: Details, Options, Remove, and the switch, as 09.15's Proposed. Options is disabled while the extension is, as its page cannot open then.
- **The engine:** `PaneBrowserContext`'s `detailsOfExtension:` gives a `PaneExtensionDetails` made as Chrome's `ExtensionInfoGenerator` makes the details page's data.
  - **The source** is the page's wording: Chrome Web Store, Unpacked extension, Added by policy, Added by a third-party, Installed by default, or Not from Chrome Web Store.
  - **The permissions** are the messages of what the extension was granted (`PermissionMessageProvider`), without its sites where the pop-up shows them.
  - **The site access** is `HostAccess`, as `CreateRuntimeHostPermissionsInfo` reads it. `setSiteAccess:forExtension:` sets it as `developerPrivate.updateExtensionConfiguration` does, through `ScriptingPermissionsModifier`.
  - **File access** is `util::AllowFileAccess`. `setAllowsFileAccess:forExtension:` is `util::SetAllowFileAccess`, which reloads the extension, as Chrome does.
  - **The size** is measured as the details page measures it (`path_util::CalculateAndFormatExtensionDirectorySize`, off the main thread). It joins the header once known, a few milliseconds after Details shows.
- **On specific sites** has no list of sites to choose from here: Paper has a proposal for one (09.16), which is not built. So the option can be chosen only while it is the current one, once the action's menu granted a site (On <site>), and shows checked then. Otherwise it is dimmed (`PaneMenuButton.isSelectable`). `PaneMenuButton` lists its options as toggles for it, the current one checked, in Settings and the profile sheet too.
- **The pop-up shows only for an extension that asks for every site,** as Chrome's. Chrome lists the sites of one that asks for some, with a switch each; Pane lists them among the permissions ("Read and change your data on …") until the site list is built.
- **Fresh:** Details reads the engine again when the extensions change, a permission change among them, and right after its own pop-up or switch, so a choice shows in the same frame.
- **Settings' cards** (`SettingsGroup` and `SettingsRow`, which Details shares) keep their rows inside their 1-point border, as Paper's CSS lays a bordered card out: each card is 2 points taller than before, as 06.1 draws it. A row's title wraps on 18-point lines where its control leaves it too little room, as Site access's long title can, rather than being cut short.

What was rejected:

- **Chrome's chrome://extensions page itself:** it is Chrome's WebUI, and Pane draws its own UI (ADR 0021).
- **A sheet or a separate window for Details:** Chrome and Aside show Details in place of the list, with a way back.
- **On specific sites always selectable:** with no list of sites, choosing it would leave the extension on click with nothing to show for it.
- **Keeping the version on the card, or moving Options into Details as Aside does:** 09.15 proposed only the version's move, and the user approved it.

How it is checked:

- **The `extension-details` scenario,** with a probe (`tests/pages/extensions/details`) that asks for every site, downloads, and notifications, and has an options page and a homepage:
  - Manage Extension opens Details: Unpacked extension, < 1 MB, Display notifications and Manage your downloads, On all sites, file access wanted, its homepage, and no Web Store page.
  - On click leaves the probe without `<all_urls>` (`chrome.permissions.getAll`). The action menu's On 127.0.0.1 shows On specific sites, with that site granted alone. On all sites gives every site back.
  - The back arrow returns to the list. View Web Permissions and a card's Details open Details again.
  - Allow access to file URLs sets Chrome's setting both ways. Chrome reloads an unpacked extension such as the probe as one it keeps disabled outside developer mode, so the probe's own view of it is not read.
  - Remove asks first, in the popup, and the page shows the list once the probe is gone.
- **On screen:** the scenario renders Details (`render`), light, dark, at On specific sites, and the list. Measured against 09.13's CSS, its cards are 68, 58, 98, 98, and 62 points with this content, 20 apart, and the switch ends 10 points in from the cards' edge. The pop-up, opened through accessibility at On all sites, offered On click and On all sites, and On specific sites disabled.
- **The `extension-management`, `extensions-menu`, `extensions-page-profile`, `extensions-per-profile`, `extension-shortcuts`, `profile-sheet`, `app-icon`, `updates`, `theme`, and `smoke` scenarios pass.**
