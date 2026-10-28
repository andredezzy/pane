# Update check via the GitHub Releases API

Pane checks for updates by asking for the repo's latest GitHub Release and comparing versions (PaneKit's `Updates`): no update framework, no update service. Installing stays a user action: Pane downloads the dmg and opens it, and the user drags Pane to Applications. The Electron app did the same, having rejected electron-updater: Pane is ad-hoc signed, Squirrel.Mac refuses to update an unsigned app, and its zip and `latest-mac.yml` would have been a publishing duty on every release for an updater that could install nothing. The contract, the `releases/latest` tag and its `-arm64.dmg` asset, is also how every Electron Pane found the native app.

Revisit with an Apple Developer ID: silent updates need signing whatever the mechanism, and an updater (Sparkle) belongs to that signing work (`native/HUMAN-CHECKLIST.md`, 6.2).
