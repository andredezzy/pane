# Proxy passwords live in the Keychain

> **Where they live is superseded by [ADR 0095](0095-proxy-passwords-sealed-under-the-engine-key.md)**: they are sealed in the data directory under the engine's key, so an update asks for the Keychain once, not twice.

A profile's proxy password is kept in the Keychain, not in plain text in `profiles.json`, which writes it as null. The password in the file was the Electron app's format; the review's safety track raised it (safety A3).

One generic password, "Pane Proxy Passwords", holds them all as JSON by profile id, so macOS asks at most once after an update of an ad-hoc signed build (ADR 0048), not once per profile. Its account is the data directory's path, so a Pane started on another data directory keeps its own. The store is `app/Pane/Security/ProxyPasswordStore.swift`, over PaneKit's `ProxyPasswords`.

The store is read at launch only when a proxy signs in (a username, or a password the file still holds), since macOS may ask first. It is written only when a password changed, on the save queue (ADR 0068), before the file: a password leaves `profiles.json` only once the store holds it. A password the file still holds, the Electron app's or an older Pane's, is read as it is, wins over the store, and moves at the first save. If the store cannot be read or written (the user denied it), the passwords stay in the file for that session rather than be lost, and the store is not asked again until the next launch.

The panic wipe deletes the item (ADR 0042). Deleting the engine's "Pane Safe Storage" key with it was rejected: that key is not scoped by data directory, so a wipe in a Pane on a throwaway data directory would sign the real one out.

Scenarios never touch the login Keychain (ADR 0013). Under `--use-mock-keychain`, the switch the engine already honors, the store is `mock-keychain.json` in the data directory. `proxy` checks that no password is left in `profiles.json` and each is in the store, and that the Electron app's plain password signs in without a prompt, moves, and signs in again after a relaunch, read back from the store. `profile-sheet` checks the same for a proxy saved from the sheet.
