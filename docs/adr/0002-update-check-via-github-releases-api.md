# Update check via GitHub Releases API, not electron-updater

Pane checks for updates by querying the repo's latest GitHub Release from the main process and comparing versions — no electron-updater, no update service. The standard electron-builder stack (electron-updater + zip target + latest-mac.yml) was deliberately rejected for now: Pane is ad-hoc signed, and Squirrel.Mac refuses to apply updates to an unsigned app, so electron-updater would add a dependency and a mandatory zip+yml publishing obligation on every release while still being unable to install anything. Installing stays a user action: Pane downloads the dmg and opens it; the user drags to Applications.

Revisit when an Apple Developer ID exists — silent auto-update requires signing regardless of mechanism, and adopting electron-updater belongs to that signing work.
