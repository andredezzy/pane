# Scenarios never hand a link to the user's apps

Scenarios never hand a link to one of the user's apps: the scenario that opens another app's link uses a scheme Chrome always asks about, answers Cancel, and checks that the app stayed closed. This follows an incident on 2026-10-26 at 07:08 local. The `external-protocol` scenario first used a `mailto:` link, which Chrome opens without asking, by design, so macOS opened the user's Mail with an unsent compose window to `pane-scenario@example.invalid`. Mail had not been running; it was quit at once (SIGTERM, 07:10), and nothing was sent.

`external-protocol` now uses `maps:`, which always asks, answers Cancel, and checks that Maps did not open; its fixed wait is there to prove that nothing opens. An entry in `docs/human-checklist.md` asks the user to look for a stray draft to that address, in case Mail autosaved the window.
