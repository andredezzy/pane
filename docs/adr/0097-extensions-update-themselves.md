# Extensions update themselves

Extensions update on their own, as in every browser, and the Extensions page has no Update all. It was the user's call (2026-10-29): the button should not exist. Chrome's extension updater already ran in Pane's engine. As each profile starts, it asks Chrome's update service (`update.googleapis.com/service/update2/json`) for newer versions of its Web Store extensions, then again every five hours or so. It installs what it finds, as Chrome does.

- **The engine:** it keeps Chrome's updater as is. `PaneBrowserContext`'s `updateExtensions`, the button's call (`ExtensionUpdater::CheckNow`), is gone, with the app's `ExtensionsController.update(inProfile:)` and the automation's `updateExtensions`.
- **The page:** the header holds the puzzle, the title, and the profile select at the trailing edge. Paper's 12 boards lost the button too.
- **A profile that is not open:** its engine is not running, so it does not check. It checks when it next opens, as a Chrome profile does.

What was rejected:

- **Keeping the button for an update now:** stable browsers have none in view. Aside's page, Chrome's own restyled (aside://extensions, checked on 2026-10-29), offers Update only under Developer mode, and the user asked for none.
- **Checking more often than Chrome:** the updater's five hours and its check as a profile starts are Chrome's, and the Web Store's servers expect them.

How it is checked:

- **The `extension-auto-update` scenario:**
  - It installs Dark Reader from the Web Store, quits, and relaunches with a network log.
  - Once the profile opens, Pane asks the update service by itself, and the request names Dark Reader (`X-Goog-Update-AppId`).
- **On the user's data** (2026-10-29), every profile with NordPass holds 7.12.15, which the Web Store's update service reports as its latest.
- **The `extension-management` scenario** passes without the button.
