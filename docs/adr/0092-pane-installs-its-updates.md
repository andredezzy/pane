# Pane installs its updates itself

Pane installs a downloaded update itself, at quit, so the user no longer drags it from the dmg. The user asked for it after an update from 0.7.0 ended on "Pane Not Opened". ADR 0002 had left installing to the user until a Developer ID, and DESIGN.md said Pane, ad-hoc signed, "can't safely replace itself in place". It can, through these steps:

1. **Check.** Once the dmg matches the release's SHA-256 and leaves quarantine (ADR 0090), Pane mounts it. The Pane inside must be this app (`com.pane.desktop`) at the offered version, with its signature whole (`codesign --verify --deep --strict`).
2. **Stage.** Pane copies it beside the running app, hidden (`.Pane-<version>.app`). The pill, Settings, and the lock screen then offer Restart to update.
3. **Swap.** Any quit installs the update; Restart to update quits on purpose. Pane swaps the two bundles in one rename (`renamex_np` with `RENAME_SWAP`), so the app's path never stands empty, and the running process keeps the files it has open.
4. **Finish.** A detached shell waits for Pane to exit, then removes the old bundle and the dmg. For Restart to update it also opens the new Pane, passing on Pane's own `PANE_` variables, which only scenarios set.
5. **Clean.** A launch removes anything an earlier install left.

Pane installs only where it runs in place: not translocated, not from a disk image, not from a folder its user cannot write. There, and whenever a step fails, the old app stays and the dmg opens as before. Staging v0.8.0 took 1.5 s (mount 0.13 s, signature 0.83 s, copy 0.46 s, detach 0.07 s), during which the pill holds its full bar.

What was rejected:

- **Sparkle:** it needs an appcast and EdDSA signing in the release pipeline, and its installer ends in the same swap.
- **Writing into the running bundle:** a Chromium app starts its helpers from that bundle while it runs.
- **Two renames:** the moment between them leaves no app at the path, and a shutdown could make that permanent.
- **An installer package:** it asks for an admin password.
- **Waiting for a Developer ID:** users would keep dragging, and keep meeting Gatekeeper when a step goes wrong.
- **Keeping the dmg in Downloads:** 190 MB that nothing needs once it is installed.

App Management (macOS 13 and later) refuses changes to an app made by code from another team, and Pane has no team. Its swap of its own bundle went through outside `/Applications`, and in it: on 2026-10-29 the user's 0.9.0, installed in `/Applications` from its dmg, installed 0.9.1 itself through Restart to update, and reopened with nothing left beside it. A refusal would leave the app as it was and open the dmg.

How it is checked:

- **PaneKit's tests:** where Pane installs over itself and what it leaves behind.
- **The `update-install` scenario:** it uses a stand-in release whose dmg holds a stand-in Pane, run on two copies of the app.
  - A dmg whose Pane is another version stays the user's.
  - A matching one is staged beside the app.
  - Restart to update swaps it in and opens it on the same data.
  - A plain quit swaps it in and opens nothing.
  - Neither leaves a staged or old bundle, and the installed dmg is gone.
- **The `updates` scenario:** its stand-in dmg is no disk image, so it falls back to opening the dmg.
- **Against the real release:** a build reporting 0.7.9 found v0.8.0 on GitHub 0.9 s after launch. It downloaded, verified, and staged the 190 MB dmg by 26.4 s, and quit 0.2 s after Restart to update with the bundle already 0.8.0. The new Pane opened 2.0 s later with its profile kept, nothing left beside it, and the dmg gone.
