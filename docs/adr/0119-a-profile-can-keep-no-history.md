# A profile can keep no history: its pages, their icons, and its downloads, and turning it off clears what it kept

The user's request (2026-11-04): each profile chooses whether it saves its history. Aside and Chrome have no such control, only Chrome's `SavingBrowserHistoryDisabled` policy, so Firefox's "Remember browsing and download history" is the reference, and the user chose what it keeps out: pages and downloads, over pages alone.

Save history is a switch in the profile sheet (ADR 0120), on by default. `BrowserProfile.savesHistory` holds the choice, and a profile from before it, which has none in its file, kept its history and still does.

What off keeps out, through `PaneBrowserContext.savesHistory`, kept in the profile's prefs:

- **The pages it visits and the visited-link colours drawn from them:** Chrome's own `history.saving_disabled`, the policy's pref.
- **Their icons:** `ContentFaviconDriver::IsOffTheRecord` answers yes (patch 0009), since the icon's mapping names the page it was found on. The page still shows its icon.
- **Its downloads:** `DownloadHistory` records none (patch 0009), extensions' included. The file still downloads, and the download still shows while Pane runs.

The tabs stay: they are the profile, not its history, and they come back after a quit. So do its cookies and site data, and a tab still goes back and forward within itself.

Turning it off clears what the profile kept, through `clearHistory`: Chrome's Clear browsing data, with Browsing history and Download history. As there, an open tab keeps its page and loses its back and forward list. Cookies stay, so the profile stays signed in. Save says what it does: "Keeps no pages or downloads. Saving clears what it kept." appears only on a profile that kept its history until this edit, since a new one, or one already off, has nothing to clear.

A closed profile turned off opens to clear, at once, as the sheet said it would. Turned on, it waits to open. Whichever way the model and the engine differ, the profile's opening makes them agree before any of its pages load (`BrowserController.applyHistory`): the engine's pref is the choice it last had, written a few seconds after the change, so a crash right after the Save, which leaves the model at off and the pref at on, clears on the next open.

Rejected:

- **Pages alone, the policy as it stands.** The download list and the icons' database would still name where the profile went.
- **An off-the-record profile.** It keeps no cookies either, so it would never stay signed in, which is what a profile is for.
- **Clearing only at the next open.** What Save said was cleared would stay on disk until then.
- **Keeping the tab's back and forward list through a clear.** That would be a Pane of its own over Chrome's remover, and those entries are part of what the profile kept.

Checked by `tests/scenarios/profile-history.ts`, against the engine's own `History` and `Favicons` databases: on, 2 pages, 1 icon, and 1 download; off from the start, none, the page's icon showing and the file downloaded; turned off, all gone, the cookie and the tab still there, and nothing new after; turned on, pages again; the choice after a quit; a closed profile clearing; and a crash right after the Save made up as the profile opens (the engine's trace shows `setSavesHistory` at that open). `ProfileDraftTests`, `BrowserModelTests`, and `PersistenceTests` check the choice's way through the form, the model, and the file.
