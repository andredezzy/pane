# Pane

Multi-profile desktop browser with per-profile proxies, fingerprint spoofing, and Chrome extension support. Pane's own UI (the chrome) wraps profile tabs that render real websites.

## Language

**Update check**:
Comparing the running app's version against the repo's latest GitHub Release. Yields at most one available update — the newest release — never a list of missed versions. Installing remains a user action (download + drag) while Pane is unsigned.
_Avoid_: auto-update (implies silent install, which unsigned macOS forbids)

**Theme**:
The app-wide appearance preference — `system`, `light`, or `dark`. One coherent knob: Pane's chrome, native menus, and what websites in profile tabs observe via `prefers-color-scheme` all follow it, matching desktop Chrome's appearance setting. `system` follows macOS.
_Avoid_: appearance, color mode, dark mode (as a feature name)

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
