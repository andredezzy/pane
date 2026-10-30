# Updates are checked every six hours

Pane checks for an update at launch and every six hours after, where it checked daily (ADR 0002). Chromium's security fixes now come every week, and a new milestone every two weeks, so a Pane left open for days must hear of a release within hours: Chrome checks every 4.5 hours, Brave every 3, and Dia every 8. A failed check still retries after 5, 15, and 60 minutes. The check is one request to GitHub's API, so six hours costs nothing a user would notice. The research into how Aside, Dia, and other Chromium browsers ship security fixes found it (2026-10-30).

What was rejected:

- **Daily, as before:** a security release could wait a day to be offered.
- **Every hour or less:** GitHub's API allows 60 unauthenticated requests an hour per address, which many Panes behind one address would share.

How it is checked:

- **PaneKit's tests** and the `updates` scenario pass with `Updates.checkInterval` at six hours.
