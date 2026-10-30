# On specific sites lists its sites, and adds and removes them as Chrome's details page does

> Replaces [ADR 0102](0102-extension-details-on-the-extensions-page.md)'s On specific sites, which could be chosen only once a site was granted and had no list.

An extension's Details, at On specific sites, list the sites it may read and change on, under the Site access pop-up, each with its ×, then Add a site, as Paper's 09.16 draws it, light and dark, approved by the user on 2026-10-30 (ADR 0058). On specific sites can always be chosen, as on chrome://extensions. Until now Pane had no way to add or see those sites: a site came only from the action menu's On <site>, and the option was dimmed without one.

- **The list:** `PaneExtensionDetails.specificSites` holds the sites Chrome's details page lists while the extension is On specific sites. These are the hosts it was granted at runtime, none inside another (`ExtensionInfoGenerator::GetDistinctHosts`), sorted as the page sorts them, in Chrome's pattern form ("*://example.com/*").
  - A row reads the pattern without its constant path, and without its scheme when that is any (`PaneKit.ExtensionSite`): example.com, *.example.com, https://example.com, http://127.0.0.1:8080.
  - The action menu's On <site> (ADR 0101) grants the page's origin, which joins the list as http://host:port.
- **Add a site** (`PaneBrowserContext`'s `addSite:toExtension:`) is the details page's Add a site.
  - What is typed goes through chrome://extensions' own pattern (`runtimeHostsPatternRegExp` and `getPatternFromSite`, in RE2): an optional http, https, or any scheme, an optional `*.`, a host or localhost, an optional port, and no path. The scheme is any when none is given. It is then parsed as developerPrivate's `ParseRuntimePermissionsPattern` parses it (http, https, and file, path `/*`).
  - A site the page refuses keeps the dialog, with Chrome's "Not a valid web address". The message goes once the site is edited.
  - An accepted site sets the extension On specific sites first, as the page's dialog does when the pop-up opened it, then grants the site (`PermissionsUpdater::GrantRuntimePermissions`), as `developerPrivate.addHostPermission` does.
- **The ×** (`removeSite:fromExtension:`) is `developerPrivate.removeHostPermission`: the site's part of what can be revoked is revoked. With no site left the extension is on click, as on Chrome's page, and the list goes.
- **On specific sites from another choice** opens Add a site first, as Chrome's page does, since the choice means nothing with no site. Cancel leaves the access as it was. The access changes only once a site is added.
- **The dialog** is the app's other dialogs' (Quit Pane?, Keyboard shortcuts): the title "Add a site", Chrome's label "Site" over the app's field, with "e.g. https://example.com" as 09.16's note gives it. Then Cancel (Esc) and Add (Return), Add waiting for something typed. A click outside cancels.
  - `KeyRouting` gives it Esc and Return, and lets the rest reach the field.
  - Paper's 09.16 described the dialog in its note rather than drawing it. 09.16.1 now draws it as built.
- **`PaneMenuButton`** is a picker again, as before ADR 0102: with no option dimmed any more, its toggles have no use.

What was rejected:

- **Chrome's list as it writes it** ("*://example.com/*"): 09.16 draws the sites as a person writes them.
- **Checking the site as it is typed,** as Chrome's dialog does: it would need a second call into the engine, or Chrome's pattern copied into the app. The message shows when Add is pressed, and goes with the next edit.
- **Choosing On specific sites with no site and then asking:** Chrome's page asks first, and the access would read On click in between.

How it is checked:

- **The `extension-details` scenario:**
  - From On click, On specific sites opens Add a site, with Details still at On click, and Cancel leaves it there.
  - "not a site" keeps the dialog, marked refused.
  - example.com is added: Details is On specific sites with *://example.com/*, and the probe has that site and no other (`chrome.permissions.getAll`).
  - The action menu's On 127.0.0.1 joins the list, and the probe has the page's origin.
  - Add a site under the list adds https://*.example.org, and its × takes it away, in the list and in the probe's sites.
  - The last ×s leave the probe on click, with no site.
  - Renders: the list and the dialog, light and dark, and the refused site. The site access card is 202 points with two sites: the pop-up's 56-point row, then three of 48, as 09.16's CSS.
- **The `extension-context-menus` scenario:** On 127.0.0.1 shows in Details' list.
- **PaneKit:** `ExtensionSiteTests`, and `KeyRoutingTests`' Add a site.
- **The `extensions-menu`, `extension-shortcuts`, `extensions-page-profile`, `profile-sheet`, `app-icon`, and `smoke` scenarios pass.**
