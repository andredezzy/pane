# Pane

Multi-profile desktop browser with per-profile proxies, fingerprint spoofing, and Chrome extension support. Pane's own UI (the chrome) wraps profile tabs that render real websites.

## Language

**Update check**:
Comparing the running app's version against the repo's latest GitHub Release. Yields at most one available update — the newest release — never a list of missed versions. Downloading it is a user action; installing happens at quit, Pane swapping the new version in for itself (Restart to update quits on purpose), and falls back to the user dragging it from the dmg where Pane cannot.
_Avoid_: auto-update (implies an install while Pane runs; Pane installs only when the user quits or restarts)

**Theme**:
The app-wide appearance preference — `system`, `light`, or `dark`. One coherent knob: Pane's chrome, native menus, and what websites in profile tabs observe via `prefers-color-scheme` all follow it, matching desktop Chrome's appearance setting. `system` follows macOS.
_Avoid_: appearance, color mode, dark mode (as a feature name)

**App icon**:
The window Pane shows in the Dock, with the sky the user picks — Day, Clouds, Morning, Dusk, Fuchsia, Sunset, Twilight, Afterglow, Night, or Moonrise — or `automatic`, which shows Day while Pane's appearance is light and Night while it is dark: the Theme decides, and under `system` the Mac. The bundle's own icon is Day, and Night under the Mac's dark icon style.
_Avoid_: logo (the brand mark in docs and Paper), theme (the appearance itself)

**Sleep (tab)**:
An inactive tab whose page has been released to reclaim memory. The tab stays in the sidebar (title, favicon, shown dimmed) and comes back by re-navigating when activated. Happens automatically after inactivity, never to a tab that is audible, recently interacted with, has DevTools open, or belongs to a Keep-loaded profile.
_Avoid_: discard, suspend (Chromium jargon), archive (Arc's archive closes the tab; sleep keeps it)

**Unload (profile)**:
Releasing a hidden profile's live state — all its tabs sleep at once. The profile and its tabs remain in the sidebar and restore on activation. Happens automatically after a profile stays hidden, or manually.
_Avoid_: close, remove (removal deletes the profile; unload never loses anything)

**Trim (cache)**:
Dropping a profile's cached bytes — HTTP cache and service worker caches — while its identity survives untouched. Cookies, local storage, and IndexedDB are never cleared, so a trimmed profile is still signed in to everything. Happens automatically when an unloading profile is over its cache budget. Budget, not quota: nothing enforces it, the scheduler checks it on unload.
_Avoid_: clear browsing data (Chrome's takes cookies too, which breaks the promise), wipe (means the security panic-wipe here), evict (Chromium jargon)

**Keep loaded**:
A per-profile exemption from automatic sleep and unload, for always-on or session-critical profiles whose pages must stay live.
_Avoid_: pin (already means something in browser tabs), whitelist

**Save history**:
A per-profile choice, on by default, of whether a profile keeps a record of where it went: the pages it visits, their icons, its downloads, and what Chrome learns from them. Turning it off clears what it kept; its cookies, site data, and tabs stay, so it stays signed in.
_Avoid_: incognito, private (those keep no cookies either), clear browsing data (Chrome's wider dialog, which this uses part of)

**Lock**:
Pane shut behind its PIN: no profile or page open, and the encrypted volume shut. With a PIN set, it comes at launch, on Lock Pane, when the Mac sleeps or locks, and after a time without use. The fifth wrong PIN at the lock wipes everything; the fifth in Settings locks Pane and erases nothing.
_Avoid_: sign out (the profiles stay signed in), pause, sleep (a tab's)

**Data key**:
The key behind everything of Pane's on disk: it opens the encrypted volume, seals `profiles.json`, and each profile's Engine key derives from it. It is kept only sealed, under the Keychain's secret and, when set, the PIN, so the PIN releases it rather than being it. The panic wipe destroys it first.
_Avoid_: PIN (what releases it), master password

**Engine key**:
A profile's own key for the files the engine writes, derived from the Data key and held only by Pane's processes, so a process reading those files finds ciphertext while Pane is unlocked as much as while it is locked. Not the key of a prebuilt engine (`engine-key.sh`, a hash of the engine's sources), which the release docs also call the engine key: say which when both are in play.
_Avoid_: profile key, cookie key (Chromium's "Pane Safe Storage")
