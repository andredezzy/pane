# An extension's Details are a view of the Extensions page, and On specific sites lists its sites, added and removed as Chrome's details page does

Each extension has its Details, as chrome://extensions and Aside's extensions page have theirs: a view inside the Extensions page, with a back arrow to the list. A card's Details button opens it, and so do Manage Extension and View Web Permissions (ADR 0101), as Chrome opens chrome://extensions/?id=. Before, Pane showed only the card, with no permissions, site access, or file access, and Manage Extension scrolled the list to the card. Paper drew it first: 09.13, 09.14 (the site access pop-up open), 09.15 (the card before and after), and 09.16 (On specific sites with its sites), light and dark, approved by the user (ADR 0058), 09.16 on 2026-10-30.

The first build had no list of sites. On specific sites could be chosen only while it was the current one, once the action's menu granted a site (On <site>), and was dimmed otherwise (`PaneMenuButton.isSelectable`, which listed its options as toggles for it, in Settings and the profile sheet too); the pop-up, opened through accessibility, offered it disabled. Choosing it with no list would have left the extension on click with nothing to show for it. The list replaced that: On specific sites can always be chosen, as on chrome://extensions, and `PaneMenuButton` is a picker again.

What changes:

- **The view** (`app/Pane/Extensions/ExtensionDetailsView.swift`), in Settings' layout (ADR 0065) and 640 points wide at most:
  - **The header:** the back arrow, the 40-point icon, the name over "Version X · <size> · <source>", and the switch.
  - **The description**, at 13 on 20-point lines.
  - **Permissions:** the extension's permission messages in Chrome's words, or Chrome's "This extension requires no special permissions".
  - **Site access:** "Allow this extension to read and change all your data on websites you visit", with the pop-up On click, On specific sites, and On all sites. At On specific sites, the sites follow under it, each with its ×, then Add a site.
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
- **The sites:** `PaneExtensionDetails.specificSites` holds the sites Chrome's details page lists while the extension is On specific sites. These are the hosts it was granted at runtime, none inside another (`ExtensionInfoGenerator::GetDistinctHosts`), sorted as the page sorts them, in Chrome's pattern form ("*://example.com/*").
  - A row reads the pattern without its constant path, and without its scheme when that is any (`PaneKit.ExtensionSite`): example.com, *.example.com, https://example.com, http://127.0.0.1:8080.
  - The action menu's On <site> (ADR 0101) grants the page's origin, which joins the list as http://host:port.
- **Add a site** (`PaneBrowserContext`'s `addSite:toExtension:`) is the details page's Add a site.
  - What is typed goes through chrome://extensions' own pattern (`runtimeHostsPatternRegExp` and `getPatternFromSite`, in RE2): an optional http, https, or any scheme, an optional `*.`, a host or localhost, an optional port, and no path. The scheme is any when none is given. It is then parsed as developerPrivate's `ParseRuntimePermissionsPattern` parses it (http, https, and file, path `/*`).
  - A site the page refuses keeps the dialog, with Chrome's "Not a valid web address". The message goes once the site is edited.
  - An accepted site sets the extension On specific sites first, as the page's dialog does when the pop-up opened it, then grants the site (`PermissionsUpdater::GrantRuntimePermissions`), as `developerPrivate.addHostPermission` does.
- **The ×** (`removeSite:fromExtension:`) is `developerPrivate.removeHostPermission`: the site's part of what can be revoked is revoked. With no site left the extension is on click, as on Chrome's page, and the list goes.
- **On specific sites from another choice** opens Add a site first, as Chrome's page does, since the choice means nothing with no site. Cancel leaves the access as it was. The access changes only once a site is added.
- **The dialog** is the app's other dialogs' (Quit Pane?, Keyboard shortcuts): the title "Add a site", Chrome's label "Site" over the app's field, with "e.g. https://example.com" as 09.16's note gives it. Then Cancel (Esc) and Add (Return), Add waiting for something typed. A click outside cancels. `KeyRouting` gives it Esc and Return, and lets the rest reach the field. Paper's 09.16 described the dialog in its note rather than drawing it; 09.16.1 now draws it as built.
- **The pop-up shows only for an extension that asks for every site,** as Chrome's. Chrome lists the sites of one that asks for some, with a switch each; Pane lists them among the permissions ("Read and change your data on …"), and those switches are not built.
- **Fresh:** Details reads the engine again when the extensions change, a permission change among them, and right after its own pop-up or switch, so a choice shows in the same frame.
- **Settings' cards** (`SettingsGroup` and `SettingsRow`, which Details shares) keep their rows inside their 1-point border, as Paper's CSS lays a bordered card out: each card is 2 points taller than before, as 06.1 draws it. A row's title wraps on 18-point lines where its control leaves it too little room, as Site access's long title can, rather than being cut short.

What was rejected:

- **Chrome's chrome://extensions page itself:** it is Chrome's WebUI, and Pane draws its own UI (ADR 0021).
- **A sheet or a separate window for Details:** Chrome and Aside show Details in place of the list, with a way back.
- **Keeping the version on the card, or moving Options into Details as Aside does:** 09.15 proposed only the version's move, and the user approved it.
- **Chrome's list as it writes it** ("*://example.com/*"): 09.16 draws the sites as a person writes them.
- **Checking the site as it is typed,** as Chrome's dialog does: it would need a second call into the engine, or Chrome's pattern copied into the app. The message shows when Add is pressed, and goes with the next edit.
- **Choosing On specific sites with no site and then asking:** Chrome's page asks first, and the access would read On click in between.

How it is checked:

- **The `extension-details` scenario,** with a probe (`tests/pages/extensions/details`) that asks for every site, downloads, and notifications, and has an options page and a homepage:
  - Manage Extension opens Details: Unpacked extension, < 1 MB, Display notifications and Manage your downloads, On all sites, file access wanted, its homepage, and no Web Store page.
  - On click leaves the probe without `<all_urls>` (`chrome.permissions.getAll`). On all sites gives every site back.
  - From On click, On specific sites opens Add a site, with Details still at On click, and Cancel leaves it there. "not a site" keeps the dialog, marked refused.
  - example.com is added: Details is On specific sites with *://example.com/*, and the probe has that site and no other. The action menu's On 127.0.0.1 joins the list, and the probe has the page's origin.
  - Add a site under the list adds https://*.example.org, and its × takes it away, in the list and in the probe's sites. The last ×s leave the probe on click, with no site.
  - The back arrow returns to the list. View Web Permissions and a card's Details open Details again.
  - Allow access to file URLs sets Chrome's setting both ways. Chrome reloads an unpacked extension such as the probe as one it keeps disabled outside developer mode, so the probe's own view of it is not read.
  - Remove asks first, in the popup, and the page shows the list once the probe is gone.
- **On screen:** the scenario renders Details (`render`), light and dark, at On specific sites with its list, the Add a site dialog, light and dark, the refused site, and the list of extensions. Measured against 09.13's CSS, its cards are 68, 58, 98, 98, and 62 points with this content, 20 apart, and the switch ends 10 points in from the cards' edge. With two sites, the site access card is 202 points: the pop-up's 56-point row, then three of 48, as 09.16's CSS.
- **The `extension-context-menus` scenario:** On 127.0.0.1 shows in Details' list.
- **PaneKit:** `ExtensionSiteTests`, and `KeyRoutingTests`' Add a site.
- **The `extension-management`, `extensions-menu`, `extensions-page-profile`, `extensions-per-profile`, `extension-shortcuts`, `profile-sheet`, `app-icon`, `updates`, `theme`, and `smoke` scenarios pass.**
